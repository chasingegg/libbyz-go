#include "crypt.h"
#include "bigint.h"
#include "sha1.h"
#include "schnorr.h"

static const u_int HASHSIZE = sha1::hashsize;

/* bind_r_to_m is just a fancy name for a procedure that
   hashes a message m and a random element of the subgroup
   of Z_p^* generated by g into a 160-bit SHA-1 hash */
void 
schnorr_pub::bind_r_to_m (bigint *e, const str &m, const bigint &r) const
{
  sha1ctx sc;
  sc.update (m.cstr (), m.len ());
  
  str r_as_str = r.getraw ();
  sc.update (r_as_str.cstr (), r_as_str.len ());
  
  char m_r_hashed[sha1::hashsize];
  sc.final (m_r_hashed);
  
  mpz_set_rawmag_le(e, m_r_hashed, sizeof (m_r_hashed));
}

/*
 * Straight-Ahead Schnorr:
 *
 *  s = (k * e^-1 + x) * e (mod q)
 */
bool
schnorr_priv::sign (bigint *r, bigint *s, const str &msg)
{
  assert (r && s);
  make_ekp ();
  if (!ekp)
    return false;
  bigint e;
  *r = ekp->public_half ();
  bind_r_to_m (&e, msg, *r);
  bigint t (invert (e, q));
  if (t < 0)
    t += q;
  t *= ekp->private_half ();
  t %= q;
  t += x;
  t *= e;
  *s = t % q;
  ekp = NULL;
  assert (check_signature (*r, *s, e, y)); // debug !!
  delaycb (0, wrap (this, &schnorr_priv::make_ekp));
  return true;
}

void
schnorr_priv::make_ekp ()
{
  if (ekp)
    return;
  ekp = make_ephem_key_pair ();
}


/* To thwart timing attacks (based on non-constant-time modular reduction
   implementation), we compute s_clnt as follows:

       s_clnt = ((k_clnt * (e^-1 mod q) + x_clnt) * e) mod q

   In this way the time will be spent almost entirely performing the two 
   modular multiplication, and in both case one factor is random, so that
   no information can be leaked about x_clnt */
bool 
schnorr_clnt_priv::complete_signature (bigint *r, bigint *s, 
				       const str &msg, 
				       const bigint &r_clnt, 
				       const bigint &k_clnt, 
				       const bigint &r_srv, 
				       const bigint &s_srv)
{
  assert (r && s);

  if (is_group_elem (r_srv)) {

    *r = (r_clnt * r_srv);
    *r %= p;

    bigint e;
    bind_r_to_m (&e, msg, *r);
  
    bigint s_clnt (invert (e, q));
    s_clnt *= k_clnt;
    s_clnt %= q;
    s_clnt += x_clnt;
    s_clnt %= q;
    s_clnt *= e;
    s_clnt %= q;

    *s = (s_clnt + s_srv);
    *s %= q; 
    
    return check_signature (*r, *s, e, y);
  }
  else
    return false;
}

/* To thwart timing attacks (see the comment to the function 
   schnorr_clnt_priv::complete_unwrapped_signature), we compute s_srv
   as follows:

       s_srv = ((k_srv * (e^-1 mod q) + x_srv) * e) mod q

   In this way the time will be spent almost entirely performing the two 
   modular multiplication, and in both case one factor is random, so that
   no information can be leaked about x_srv */
bool
schnorr_srv_priv::endorse_signature (bigint *r_srv, bigint *s_srv,
				     const str &msg, 
				     const bigint &r_clnt)
{
  assert ((r_srv != NULL) && (s_srv != NULL));


  if (is_group_elem (r_clnt)) {

    // server's ephemeral public key, private key pair
    ref<ephem_key_pair> ekp_srv = make_ephem_key_pair ();
    *r_srv = ekp_srv->public_half ();

    // combine client's and server's ephemeral public keys
    bigint r (r_clnt * (*r_srv));
    r %= p;
    
    bigint e;
    bind_r_to_m (&e, msg, r);
    
    *s_srv  = invert (e, q);
    *s_srv *= ekp_srv->private_half ();
    *s_srv %= q;
    *s_srv += x_srv;
    *s_srv %= q;
    *s_srv *= e;
    *s_srv %= q;

    return true;
  }
  else
    return false;
}

#define DIV_ROUNDUP(p,q) ((p) / (q) + ((p) % (q) == 0 ? 0 : 1))

ptr<schnorr_gen>
schnorr_gen::rgen (u_int pbits, u_int iter)
{
  ptr<schnorr_gen> sgt = New refcounted<schnorr_gen> (pbits);
  sgt->seedsize = DIV_ROUNDUP (HASHSIZE, 8) + 1; // 8 bytes in a u_int64_t
  sgt->seed = New u_int64_t[sgt->seedsize];
  for (u_int i = 0; i < sgt->seedsize; i++)
    sgt->seed[i] = rnd.gethyper ();
  sgt->gen (iter);
  return sgt;
}

schnorr_gen::schnorr_gen (u_int p) : seed (NULL), pbits (p)
{
  pbytes = p >> 3;
  num_p_hashes = DIV_ROUNDUP (pbytes, HASHSIZE);
  raw_psize = num_p_hashes * HASHSIZE;
  raw_p = New char[raw_psize];
  num_p_candidates = pbits << 2;  // shouldn't fail -- 4x expected # of trials
}

void
schnorr_gen::gen (u_int iter)
{
  bigint q, p, g, y, x, x_c, x_s;
  do {
    gen_q (&q);
  } while (!gen_p (&p, q, iter) || !q.probab_prime (iter));
  gen_g (&g, p, q);

  x_c = random_zn (q);
  x_s = random_zn (q);
  x = x_c + x_s;
  x %= q;
  y = powm (g, x, p);

  csk = New refcounted<schnorr_clnt_priv> (p, q, g, y, x_c);
  ssk = New refcounted<schnorr_srv_priv>  (p, q, g, y, x_s);
  wsk = New refcounted<schnorr_priv>      (p, q, g, y, x);
}

void
schnorr_gen::gen_q (bigint *q)
{
  bigint u1, u2;
  char digest[HASHSIZE];
  do {
    sha1_hash (digest, seed, seedsize << 3); // seedsize * 8
    mpz_set_rawmag_le (&u1, digest, HASHSIZE);
    seed[3]++;
    sha1_hash (digest, seed, seedsize << 3); // seedsize * 8
    mpz_set_rawmag_le (&u2, digest, HASHSIZE);
    mpz_xor (q, &u1, &u2);
    mpz_setbit (q, (HASHSIZE << 3) - 1);     // set high bit
    mpz_setbit (q, 0);                       // set low bit
  } while (!q->probab_prime (5));
}

bool
schnorr_gen::gen_p (bigint *p, const bigint &q, u_int iter)
{
  bigint X, c;
  for (u_int i = 0; i < num_p_candidates; i++) {
    for (u_int off = 0; off < raw_psize; off += HASHSIZE) {
      seed[0]++;
      sha1_hash (raw_p + off, seed, seedsize << 3); // seedsize * 8
    }
    mpz_set_rawmag_le (&X, raw_p, pbytes);
    mpz_setbit (&X, pbits - 1);
    c = X;
    c = mod (c, q * 2);
    *p = (X - c + 1);

    if (p->probab_prime (iter))
      return true;
  }
  return false;
}

void
schnorr_gen::gen_g (bigint *g, const bigint &p, const bigint &q)
{
  bigint e = (p - 1) / q;
  bigint h;
  bigint p_3 = p - 3;
 
  do h = random_zn (p_3);
  while ((*g = powm (++h, e, p)) == 1);
}

ptr<schnorr_clnt_priv>
schnorr_clnt_priv::update (bigint *deltap) const
{
  bigint delta;
  if (deltap && (*deltap > 0))
    delta = *deltap;
  else {
    delta = random_zn (q);
    if (deltap)
      *deltap = delta;
  }
  bigint nx_c = x_clnt + q;
  nx_c -= delta;
  nx_c %= q;
  return New refcounted <schnorr_clnt_priv> (p, q, g, y, nx_c);
}

ptr<schnorr_srv_priv>
schnorr_srv_priv::update (const bigint &dlt) const
{
  bigint nx_c = x_srv + dlt;
  nx_c %= q;
  return New refcounted <schnorr_srv_priv> (p, q, g, y, nx_c);
}
