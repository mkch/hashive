package impl

import (
	"math"
	"math/big"
)

func isPrimeMillerRabin(n int) bool {
	// https://en.wikipedia.org/wiki/Miller%E2%80%93Rabin_primality_test
	// if n < 2^64 = 18,446,744,073,709,551,616, it is enough to test a = 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, and 37.
	return big.NewInt(int64(n)).ProbablyPrime(12) // panics if n < 0.
}

// nearestPrime returns the prime number nearest to n.
// Panics if n < 0.
func nearestPrime(n int) (prime int) {
	if n < 0 {
		panic("negative n for nearestPrime")
	}
	if n <= 2 {
		return 2
	}
	for i := n; i > 0 && i <= math.MaxInt; i++ {
		if isPrimeMillerRabin(i) {
			return i
		}
	}
	for i := n; i > 2; i-- {
		if isPrimeMillerRabin(i) {
			return i
		}
	}
	panic("can't find the nearest prime") // should not happen.
}
