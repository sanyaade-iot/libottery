/* Libottery by Nick Mathewson.

   This software has been dedicated to the public domain under the CC0
   public domain dedication.

   To the extent possible under law, the person who associated CC0 with
   libottery has waived all copyright and related or neighboring rights
   to libottery.

   You should have received a copy of the CC0 legalcode along with this
   work in doc/cc0.txt.  If not, see
      <http://creativecommons.org/publicdomain/zero/1.0/>.
 */
#ifndef OTTERY_INTERNAL_H_HEADER_INCLUDED_
#define OTTERY_INTERNAL_H_HEADER_INCLUDED_
#include <stdint.h>
#include <sys/types.h>
#include "ottery-config.h"
#include "ottery-threading.h"

/** Largest possible state_bytes value. */
#define MAX_STATE_BYTES 64
/** Largest possible state_len value. */
#define MAX_STATE_LEN 256
/** Largest possible output_len value. */
#define MAX_OUTPUT_LEN 1024

/**
 * @brief Flags for external entropy sources.
 *
 * @{ */
/** An RNG that probably provides strong entropy. */
#define OTTERY_ENTROPY_FL_STRONG          0x000001
/** An RNG that runs very quickly. */
#define OTTERY_ENTROPY_FL_FAST            0x000002
/** @} */

/**
 * @brief Identifying external entropy domains.
 */
/** An RNG provided by the operating system. */
#define OTTERY_ENTROPY_DOM_OS             0x000100
/** An RNG provided by the CPU. */
#define OTTERY_ENTROPY_DOM_CPU            0x000200
/** An EGD-style entropy source */
#define OTTERY_ENTROPY_DOM_EGD            0x000400
/** @} */

#define OTTERY_ENTROPY_DOM_MASK           0x00ff00

/**
 * @brief External entropy sources
 *
 * @{ */
/** A unix-style /dev/urandom device. */
#define OTTERY_ENTROPY_SRC_RANDOMDEV      0x0010000
/** The Windows CryptGenRandom call. */
#define OTTERY_ENTROPY_SRC_CRYPTGENRANDOM 0x0020000
/** The Intel RDRAND instruction. */
#define OTTERY_ENTROPY_SRC_RDRAND         0x0040000
/** DOCDOC */
#define OTTERY_ENTROPY_SRC_EGD            0x0080000
/** @} */

#define OTTERY_ENTROPY_ALL_SOURCES        0x0fff0000

struct sockaddr;

/** Configuration for the strong RNG the we use for entropy. */
struct ottery_osrng_config {
  /** The filename to use as /dev/urandom. Ignored if this
   * is not a unix-like operating system. If this is NULL, we use
   * the default value. */
  const char *urandom_fname;
  /** DOCDOC */
  struct sockaddr *egd_sockaddr;
  int egd_socklen;
  /** DOCDOC */
  uint32_t disabled_sources;
};

/**
 * Return the buffer size to allocate when getting at least n bytes from each
 * entropy source.  We might not actually need so many. */
size_t ottery_get_entropy_bufsize_(size_t n);

/**
 * Interface to underlying strong RNGs.  If this were fast, we'd just use it
 * for everything, and forget about having a userspace PRNG.  Unfortunately,
 * it typically isn't.
 *
 * @param config A correctly set-up ottery_osrng_config.
 * @param require_flags Only run entropy sources with *all* of these
 *      OTTERY_ENTROPY_* flags set. Set this to 0 to use all the sources
 *      that work.
 * @param bytes A buffer to receive random bytes.
 * @param n The number of bytes to try to get from each entropy source.
 * @param bufsize The number of bytes available in the buffer; modified
 *      to hold the number of bytes actually written.
 * @param flags_out Set to a bitwise OR of all of the OTTERY_ENTROPY_* flags
 *      for sources in the result.
 * @return Zero on success, or an error code on failure. On failure, it is not
 *   safe to treat the contents of the buffer as random at all.
 */
int ottery_get_entropy_(const struct ottery_osrng_config *config,
                         uint32_t require_flags,
                         uint8_t *bytes, size_t n, size_t *bufsize,
                         uint32_t *flags_out);

/**
 * Clear all bytes stored in a structure. Unlike memset, the compiler is not
 * going to optimize this out of existence because the target is about to go
 * out of scope.
 *
 * @param mem Pointer to the memory to erase.
 * @param len The number of bytes to erase.
 */
void ottery_memclear_(void *mem, size_t len);

/**
 * Information on a single pseudorandom function that we can use to generate
 * a bytestream which (we hope) an observer can't distinguish from random
 * bytes.
 *
 * Broadly speaking, every ottery_prf has an underlying function from an
 * (state_bytes)-byte state and a 4 byte counter to an output_len-byte
 * output block.
 **/
struct ottery_prf {
  /** The name of this algorithm. */
  const char *name;
  /** The name of the implementation of this algorithm*/
  const char *impl;
  /** The name of the flavor of the implementation of this algorithm*/
  const char *flav;
  /** The length of the object that's used to hold the state (keys, nonces,
   * subkeys as needed, etc) for this PRF. This can be longer than
   * state_bytes because of key expansion or structure padding.  It must be
   * no greater than MAX_STATE_LEN. */
  unsigned state_len;
  /** The number of bytes used to generate a state object. It must be no
   * greater than MAX_STATE_BYTES.  It must be no grater than output_len. */
  unsigned state_bytes;
  /** The number of bytes generated by a single call to the generate
   * function. It must be no larger than MAX_OUTPUT_LEN.
   */
  unsigned output_len;
  /** Bitmask of CPU flags required to run this PRF. */
  uint32_t required_cpucap;
  /** Pointer to a function to intialize a state structure for the PRF.
   *
   * @param state An object of size at least (state_len) that will
   *     hold the state and any derived values.  It must be aligned to
   *     a 16-byte boundary.
   * @param bytes An array of (state_bytes) random bytes.
   */
  void (*setup)(void *state, const uint8_t *bytes);
  /** Pointer to a function that calculates the PRF.
   *
   * @param state A state object previously initialized by the setup
   *     function.
   * @param output An array of (output_len) bytes in which to store the
   *     result of the function
   * @param idx A counter value for the function.
   */
  void (*generate)(void *state, uint8_t *output, uint32_t idx);
};

#ifdef OTTERY_INTERNAL
struct ottery_config {
  /** The PRF that we should use.  If NULL, we use the default. */
  const struct ottery_prf *impl;

  /** The filename for urandom to use. If NULL, we use the default. */
  const char *urandom_fname;

  /** Don't use any sources with *any* of these flags set. */
  uint32_t disabled_sources;
};

#define ottery_state_nolock ottery_state

struct __attribute__((aligned(16))) ottery_state {
  /**
   * Holds up to prf.output_len bytes that have been generated by the
   * pseudorandom function. */
  __attribute__ ((aligned (16))) uint8_t buffer[MAX_OUTPUT_LEN];
  /**
   * Holds the state information (typically nonces and keys) used by the
   * pseudorandom function. */

  __attribute__ ((aligned (16))) uint8_t state[MAX_STATE_LEN];
  /**
   * Parameters and function pointers for the cryptographic pseudorandom
   * function that we're using. */
  struct ottery_prf prf;
  /**
   * Index of the *next* block counter to use when generating random bytes
   * with prf.  When this equals or exceeds prf.stir_after, we should stir
   * the PRNG. */
  uint32_t block_counter;
  /**
   * Magic number; used to tell whether this state is initialized.
   */
  uint32_t magic;
  /**
   * Index of the next byte in (buffer) to yield to the user.
   *
   * Invariant: this is less than prf.output_len. */
  uint16_t pos;
  /**
   * The pid of the process in which this PRF was most recently seeded
   * from the OS. We use this to avoid use-after-fork problems; see
   * ottery_st_rand_lock_and_check(). */
  pid_t pid;
  /**
   * Combined flags_out results from all calls to the entropy source that
   * have influenced our current state.
   */
  uint32_t entropy_src_flags;
  /**
   * flags_out result from our last call to the entropy source.
   */
  uint32_t last_osrng_flags;
  /**
   * Configuration and state for the entropy source.
   */
  struct ottery_osrng_config osrng_config;
  /**
   * @brief Locks for this structure.
   *
   * This lock will not necessarily be recursive.  It's probably a
   * spinlock.
   *
   * @{
   */
DECL_LOCK(mutex);
  /**@}*/
};
#endif

struct ottery_config;
/**
 * For testing: manually supply a PRF.
 */
void ottery_config_set_manual_prf_(struct ottery_config *cfg,
                                   const struct ottery_prf *prf);

/**
 * For testing: override the use of /dev/urandom for initial
 * RNG seeding.  The fname pointe must remain value for the lifetime
 * of the ottery state.  Has no effect when /dev/urandom is not used.
 */
void ottery_config_set_urandom_device_(struct ottery_config *cfg,
                                       const char *fname);

/**
 * For testing: DOCDOC.
 */
void ottery_config_disable_entropy_sources_(struct ottery_config *cfg,
                                            uint32_t disabled_sources);

/** Called when a fatal error has occurred: Die horribly, or invoke
 * ottery_fatal_handler. */
void ottery_fatal_error_(int error);

#define OTTERY_CPUCAP_SIMD (1<<0)
#define OTTERY_CPUCAP_SSSE3 (1<<1)
#define OTTERY_CPUCAP_AES  (1<<2)
#define OTTERY_CPUCAP_RAND (1<<3)

/** Return a mask of OTTERY_CPUCAP_* for what the CPU will offer us. */
uint32_t ottery_get_cpu_capabilities_(void);

/** Tell ottery_get_cpu_capabilities to never report certain capabilities as
 * present. */
void ottery_disable_cpu_capabilities_(uint32_t disable);

/**
 * @brief pure-C portable ChaCha implementations.
 *
 * @{
 */
extern const struct ottery_prf ottery_prf_chacha8_merged_;
extern const struct ottery_prf ottery_prf_chacha12_merged_;
extern const struct ottery_prf ottery_prf_chacha20_merged_;
/**@}*/

/**
 * @brief SIMD-basd ChaCha implementations.
 *
 * These are much, much faster.
 *
 * @{ */
#ifdef HAVE_SIMD_CHACHA
extern const struct ottery_prf ottery_prf_chacha8_krovetz_1_;
extern const struct ottery_prf ottery_prf_chacha12_krovetz_1_;
extern const struct ottery_prf ottery_prf_chacha20_krovetz_1_;
#endif

#ifdef HAVE_SIMD_CHACHA_2
extern const struct ottery_prf ottery_prf_chacha8_krovetz_2_;
extern const struct ottery_prf ottery_prf_chacha12_krovetz_2_;
extern const struct ottery_prf ottery_prf_chacha20_krovetz_2_;
#endif
/** @} */

#endif
