package hashive_test

import (
	"bytes"
	"os"
	"path/filepath"
	"reflect"
	"testing"

	"github.com/mkch/hashive"
)

func TestWriteRead(t *testing.T) {
	const filename = "testdata/test.hashive"
	os.MkdirAll(filepath.Dir(filepath.Clean(filename)), 0777)
	defer os.Remove(filename)

	type Addr struct {
		Line1 string
		Line2 string
	}

	err := hashive.WriteFile(filename, map[string]any{
		"name":    "mkch",
		"age":     18,
		"hobbies": []any{"programming", "ping-pong"},
		"addr":    Addr{"line1", "line2"},
	})
	if err != nil {
		t.Fatal(err)
	}

	h, close, err := hashive.Open(filename, 64)
	if err != nil {
		t.Fatal(err)
	}
	defer close()

	v, err := h.Query("name")
	if err != nil {
		t.Fatal(err)
	} else if name := v.(string); name != "mkch" {
		t.Fatal(name)
	}

	v, err = h.Query("age")
	if err != nil {
		t.Fatal(err)
	} else if age := v.(int64); age != int64(18) {
		t.Fatal(age)
	}

	v, err = h.Query("hobbies", "0x1")
	if err != nil {
		t.Fatal(err)
	} else if hobby := v.(string); hobby != "ping-pong" {
		t.Fatal(hobby)
	}

	var addr Addr
	err = h.QueryGob(&addr, "addr")
	if err != nil {
		t.Fatal(err)
	} else if addr != (Addr{"line1", "line2"}) {
		t.Fatal(addr)
	}

}

func TestWriteJSONString(t *testing.T) {
	tests := []struct {
		name    string
		arg     string
		wantW   any
		wantErr bool
	}{
		{"null", `null`, nil, false},
		{"number", `123`, float64(123), false},
		{"string", `"123"`, "123", false},
		{"array", `["123", 123, {"k":true}]`, []any{"123", float64(123), map[string]any{"k": true}}, false},
		{"empty object", `{}`, map[string]any{}, false},
		{"object",
			`{"1":2, "ary":[], "obj_ary":[{"3":4}]}`,
			map[string]any{"1": float64(2), "ary": []any{}, "obj_ary": []any{map[string]any{"3": float64(4)}}},
			false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			w := &bytes.Buffer{}
			if err := hashive.WriteJSONString(w, tt.arg); (err != nil) != tt.wantErr {
				t.Fatalf("WriteJSONString() error = %v, wantErr %v", err, tt.wantErr)
			}

			if h, err := hashive.New(bytes.NewReader(w.Bytes()), 6); err != nil {
				t.Fatal(err)
			} else if v, err := h.Query(); err != nil {
				t.Fatal(err)
			} else if !reflect.DeepEqual(v, tt.wantW) {
				t.Fatalf("Query() = %v, want %v", v, tt.wantW)
			}
		})
	}
}
