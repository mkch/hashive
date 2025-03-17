package impl

import (
	"math"
	"testing"
)

func Test_nearestPrime(t *testing.T) {
	type want struct {
		prime   int
		smaller bool
	}
	tests := []struct {
		name string
		arg  int
		want want
	}{
		{"0", 0, want{2, false}},
		{"1", 1, want{2, false}},
		{"8", 8, want{11, false}},
		{"MaxInt32", math.MaxInt32, want{math.MaxInt32, false}},
		{"2147466428", 2147466428, want{2147466439, false}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := func() want { prime, smaller := nearestPrime(tt.arg); return want{prime, smaller} }(); got != tt.want {
				t.Errorf("nearest() = %v, want %v", got, tt.want)
			}
		})
	}
}
