package impl

import (
	"math"
	"testing"
)

func Test_nearestPrime(t *testing.T) {
	tests := []struct {
		name string
		arg  int
		want int
	}{
		{"0", 0, 2},
		{"1", 1, 2},
		{"8", 8, 11},
		{"MaxInt32", math.MaxInt32, math.MaxInt32},
		{"2147466428", 2147466428, 2147466439},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := nearestPrime(tt.arg); got != tt.want {
				t.Errorf("nearest() = %v, want %v", got, tt.want)
			}
		})
	}
}
