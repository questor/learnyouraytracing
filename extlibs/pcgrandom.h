
// taken from http://www.pcg-random.org/
// src https://github.com/imneme/pcg-c-basic

#ifndef __PCGRANDOM_H__
#define __PCGRANDOM_H__

#include <math.h>
#include <inttypes.h>

class PCGRandom {
   struct pcg_state_setseq_64 {    // Internals are *Private*.
       uint64_t state;             // RNG state.  All values are possible.
       uint64_t inc;               // Controls which RNG sequence (stream) is
                                   // selected. Must *always* be odd.
   };
public:
   PCGRandom() {
      mState.state = 0x853c49e6748fea9bULL;
      mState.inc = 0xda3e39cb94b95bdbULL;
   }

   // seed(initstate, initseq)
   //     Seed the rng.  Specified in two parts, state initializer and a
   //     sequence selection constant (a.k.a. stream id)
   void seed(uint64_t initstate, uint64_t initseq) {
       mState.state = 0U;
       mState.inc = (initseq << 1u) | 1u;
       random();
       mState.state += initstate;
       random();
   }

   // random()
   //     Generate a uniformly distributed 32-bit random number
   uint32_t random() {
       uint64_t oldstate = mState.state;
       mState.state = oldstate * 6364136223846793005ULL + mState.inc;
       uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
       uint32_t rot = oldstate >> 59u;
       return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
   }

   // boundedrand(bound):
   //     Generate a uniformly distributed number, r, where 0 <= r < bound
   uint32_t boundedrand(uint32_t bound) {
       // To avoid bias, we need to make the range of the RNG a multiple of
       // bound, which we do by dropping output less than a threshold.
       // A naive scheme to calculate the threshold would be to do
       //
       //     uint32_t threshold = 0x100000000ull % bound;
       //
       // but 64-bit div/mod is slower than 32-bit div/mod (especially on
       // 32-bit platforms).  In essence, we do
       //
       //     uint32_t threshold = (0x100000000ull-bound) % bound;
       //
       // because this version will calculate the same modulus, but the LHS
       // value is less than 2^32.

       uint32_t threshold = -bound % bound;

       // Uniformity guarantees that this loop will terminate.  In practice, it
       // should usually terminate quickly; on average (assuming all bounds are
       // equally likely), 82.25% of the time, we can expect it to require just
       // one iteration.  In the worst case, someone passes a bound of 2^31 + 1
       // (i.e., 2147483649), which invalidates almost 50% of the range.  In 
       // practice, bounds are typically small and only a tiny amount of the range
       // is eliminated.
       for (;;) {
           uint32_t r = random();
           if (r >= threshold)
               return r % bound;
       }
   }

   double randomf() {
      //generates floating point values in range [0,1) that has been rounded down to
      // the nearest multiple of 1/2**32
      return ldexp(random(), -32);
   }

  // [+offset ..  +offset+range]
  float rndPos(float range, float offset) {
    return randomf()*range + offset;
  }
  // [-range/2 .. +range/2]
  float rndPosNeg(float range) {
    return randomf()*range - (range/2.0f);
  }

  // [-offset-range/2 .. -offset] [+offset .. +offset+range/2]
  float rndPosNeg(float range, float offset) {
    float f = randomf()*range - (range/2.0f);
    if(f >= 0.0f) {
      return f+offset;
    } else {
      return f-offset;
    }
  }

protected:
   pcg_state_setseq_64 mState;
};

#endif   //#ifndef __PCGRANDOM_H__
