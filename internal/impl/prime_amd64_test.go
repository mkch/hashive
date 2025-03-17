package impl

import (
	"testing"
)

func Test_nearestPrime2(t *testing.T) {
	const maxPrimeInt = 9223372036854775783
	n, smaller := nearestPrime(maxPrimeInt - 1)
	if n != maxPrimeInt || smaller {
		t.Fatal(n, smaller)
	}
	n, smaller = nearestPrime(maxPrimeInt + 1)
	if n != maxPrimeInt || !smaller {
		t.Fatal(n, smaller)
	}
}
