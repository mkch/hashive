package impl

import (
	"bytes"
	"errors"
	"io"
	"reflect"
	"testing"
)

func TestWriteUint(t *testing.T) {
	tests := []struct {
		name    string
		arg     uint64
		wantW   []byte
		wantErr bool
	}{
		{"0", 0, []byte{byte(typeUint), 0}, false},
		{"7", 7, []byte{byte(typeUint), 7}, false},
		{"0xFF-1", 0xFF - 1, []byte{byte(typeUint), 0xFF, 0xFE}, false},
		{"0xFF+1", 256, []byte{byte(typeUint), 0xFE, 0x00, 0x01}, false},
		{"0xFFFF+1", 0xFFFF + 1, []byte{byte(typeUint), 0xFD, 0x00, 0x00, 0x01}, false},
		{"0xFFFFFF+1", 0xFFFFFF + 1, []byte{byte(typeUint), 0xFC, 0x00, 0x00, 0x00, 0x01}, false},
		{"0xFFFFFFFF+1", 0xFFFFFFFF + 1, []byte{byte(typeUint), 0xFB, 0x00, 0x00, 0x00, 0x00, 0x01}, false},
		{"0xFFFFFFFFFF+1", 0xFFFFFFFFFF + 1, []byte{byte(typeUint), 0xFA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, false},
		{"0xFFFFFFFFFFFF+1", 0xFFFFFFFFFFFF + 1, []byte{byte(typeUint), 0xF9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, false},
		{"0xFFFFFFFFFFFFFF+1", 0xFFFFFFFFFFFFFF + 1, []byte{byte(typeUint), 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			w := &bytes.Buffer{}
			if err := WriteUint(w, tt.arg); (err != nil) != tt.wantErr {
				t.Errorf("WriteUint() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if gotW := w.Bytes(); !bytes.Equal(gotW, tt.wantW) {
				t.Errorf("WriteUint() = %v, want %v", gotW, tt.wantW)
			}
		})
	}
}

func TestReadUint(t *testing.T) {
	tests := []struct {
		name    string
		arg     []byte
		wantN   uint64
		wantErr bool
	}{
		{"0", []byte{byte(typeUint), 0}, 0, false},
		{"7", []byte{byte(typeUint), 7}, 7, false},
		{"0xFF-1", []byte{byte(typeUint), 0xFF, 0xFE}, 0xFF - 1, false},
		{"0xFF+1", []byte{byte(typeUint), 0xFE, 0x00, 0x01}, 256, false},
		{"0xFFFF+1", []byte{byte(typeUint), 0xFC, 0x00, 0x00, 0x01, 0x00}, 0xFFFF + 1, false},
		{"0xFFFFFFFF+1", []byte{byte(typeUint), 0xF8, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}, 0xFFFFFFFF + 1, false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			gotN, err := ReadUint(bytes.NewReader(tt.arg))
			if (err != nil) != tt.wantErr {
				t.Errorf("ReadUint() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if gotN != tt.wantN {
				t.Errorf("ReadUint() = %v, want %v", gotN, tt.wantN)
			}
		})
	}
}

func TestWriteBool(t *testing.T) {
	tests := []struct {
		name    string
		arg     bool
		wantW   []byte
		wantErr bool
	}{
		{"true", true, []byte{byte(typeBool), 1}, false},
		{"false", false, []byte{byte(typeBool), 0}, false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			w := &bytes.Buffer{}
			if err := WriteBool(w, tt.arg); (err != nil) != tt.wantErr {
				t.Errorf("WriteBool() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if gotW := w.Bytes(); !bytes.Equal(gotW, tt.wantW) {
				t.Errorf("WriteBool() = %v, want %v", gotW, tt.wantW)
			}
		})
	}
}

func TestReadBool(t *testing.T) {
	tests := []struct {
		name    string
		arg     []byte
		wantB   bool
		wantErr bool
	}{
		{"true", []byte{byte(typeBool), 1}, true, false},
		{"false", []byte{byte(typeBool), 0}, false, false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			gotB, err := ReadBool(bytes.NewReader(tt.arg))
			if (err != nil) != tt.wantErr {
				t.Errorf("ReadBool() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if gotB != tt.wantB {
				t.Errorf("ReadBool() = %v, want %v", gotB, tt.wantB)
			}
		})
	}
}

func TestWriteInt(t *testing.T) {
	tests := []struct {
		name    string
		arg     int64
		wantW   []byte
		wantErr bool
	}{
		{"0", 0, []byte{byte(typeInt), 0}, false},
		{"-129", -129, []byte{byte(typeInt), 0xFE, 0x01, 0x01}, false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			w := &bytes.Buffer{}
			if err := WriteInt(w, tt.arg); (err != nil) != tt.wantErr {
				t.Errorf("WriteInt() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if gotW := w.Bytes(); !bytes.Equal(gotW, tt.wantW) {
				t.Errorf("WriteInt() = %v, want %v", gotW, tt.wantW)
			}
		})
	}
}

func TestReadInt(t *testing.T) {
	tests := []struct {
		name    string
		arg     []byte
		wantN   int64
		wantErr bool
	}{
		{"0", []byte{byte(typeInt), 0}, 0, false},
		{"-129", []byte{byte(typeInt), 0xFE, 0x01, 0x01}, -129, false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			gotN, err := ReadInt(bytes.NewReader(tt.arg))
			if (err != nil) != tt.wantErr {
				t.Errorf("ReadInt() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if gotN != tt.wantN {
				t.Errorf("ReadInt() = %v, want %v", gotN, tt.wantN)
			}
		})
	}
}

func TestWriteFloat(t *testing.T) {
	tests := []struct {
		name    string
		arg     float64
		wantW   []byte
		wantErr bool
	}{
		{"0.0", 0.0, []byte{byte(typeFloat), 0x0}, false},
		{"17.0", 17.0, []byte{byte(typeFloat), 0xFE, 0x40, 0x31}, false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			w := &bytes.Buffer{}
			if err := WriteFloat(w, tt.arg); (err != nil) != tt.wantErr {
				t.Errorf("WriteFloat() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if gotW := w.Bytes(); !bytes.Equal(gotW, tt.wantW) {
				t.Errorf("WriteFloat() = %v, want %v", gotW, tt.wantW)
			}
		})
	}
}

func TestReadFloat(t *testing.T) {
	tests := []struct {
		name    string
		arg     []byte
		wantF   float64
		wantErr bool
	}{
		{"0.0", []byte{byte(typeFloat), 0x0}, 0.0, false},
		{"17.0", []byte{byte(typeFloat), 0xFE, 0x40, 0x31}, 17.0, false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			gotF, err := ReadFloat(bytes.NewReader(tt.arg))
			if (err != nil) != tt.wantErr {
				t.Errorf("ReadFloat() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if gotF != tt.wantF {
				t.Errorf("ReadFloat() = %v, want %v", gotF, tt.wantF)
			}
		})
	}
}

func TestWriteBinary(t *testing.T) {
	tests := []struct {
		name    string
		arg     []byte
		wantW   []byte
		wantErr bool
	}{
		{"[1,2,3]", []byte{1, 2, 3}, []byte{byte(typeBinary), 3, 1, 2, 3}, false},
		{"len 0xFF-1", bytes.Repeat([]byte{1}, 0xFF-1), append([]byte{byte(typeBinary), 0xFF, 0xFE}, bytes.Repeat([]byte{1}, 0xFF-1)...), false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			w := &bytes.Buffer{}
			if err := WriteBinary(w, tt.arg); (err != nil) != tt.wantErr {
				t.Errorf("WriteBinary() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if gotW := w.Bytes(); !bytes.Equal(gotW, tt.wantW) {
				t.Errorf("WriteBinary() = %v, want %v", gotW, tt.wantW)
			}
		})
	}
}

func TestReadBinary(t *testing.T) {
	tests := []struct {
		name    string
		arg     []byte
		wantP   []byte
		wantErr bool
	}{
		{"[1,2,3]", []byte{byte(typeBinary), 3, 1, 2, 3}, []byte{1, 2, 3}, false},
		{"len 0xFF-1", append([]byte{byte(typeBinary), 0xFF, 0xFE}, bytes.Repeat([]byte{1}, 0xFF-1)...), bytes.Repeat([]byte{1}, 0xFF-1), false},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			gotP, err := ReadBinary(bytes.NewReader(tt.arg))
			if (err != nil) != tt.wantErr {
				t.Errorf("ReadBinary() error = %v, wantErr %v", err, tt.wantErr)
				return
			}
			if !reflect.DeepEqual(gotP, tt.wantP) {
				t.Errorf("ReadBinary() = %v, want %v", gotP, tt.wantP)
			}
		})
	}
}

func TestReadWriteGob(t *testing.T) {
	gobEncoder, gobDecoder := NewGobEncoder(), NewGobDecoder()
	var buf bytes.Buffer
	type s struct {
		A int
		B string
	}
	var toEncode = s{1, "111"}

	err := WriteGob(&buf, toEncode, gobEncoder)
	if err != nil {
		t.Fatal(err)
	}

	var decoded s
	v, err := ReadGob(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatal(err)
	}
	err = gobDecoder(v, &decoded)
	if err != nil {
		t.Fatal(err)
	} else if decoded != toEncode {
		t.Fatal(decoded)
	}
}

func TestReadWriteArray(t *testing.T) {
	gobEncoder := NewGobEncoder()
	ary := []any{
		int64(1), int64(256), int64(-123),
		true,
		"abc",
		1.625,
		[]byte{1, 2, 3},
		[]any{int64(1), "2", []any{}},
		map[string]any{"1": "123"},
	}

	var buf bytes.Buffer
	err := WriteArray(&buf, ary, gobEncoder)
	if err != nil {
		t.Fatal(err)
	}

	readAry, err := ReadArray(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatal(err)
	}

	if v, err := readAry.Index(2, true); err != nil {
		t.Fatal(err)
	} else if v != int64(-123) {
		t.Fatal(v)
	}

	var boundsErr *BoundsError
	if v, err := readAry.Index(99, true); !errors.As(err, &boundsErr) {
		t.Fatal(err)
	} else if v != nil {
		t.Fatal(v)
	}

	read, err := readAry.Value()
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(ary, read) {
		t.Fatal(read)
	}
}

func TestReadWriteObject(t *testing.T) {
	gobEncoder := NewGobEncoder()
	obj := map[string]any{
		"true": true,
		"123":  int64(123),
		"456":  []byte{4, 5, 6},
		"789": map[string]any{
			"ary": []any{"abc", 0.625},
		},
	}
	var buf bytes.Buffer
	err := WriteObject(&buf, obj, gobEncoder)
	if err != nil {
		t.Fatal(err)
	}

	readObj, err := ReadObject(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatal(err)
	}
	if v, err := readObj.Index("123", true); err != nil {
		t.Fatal(err)
	} else if v != int64(123) {
		t.Fatal(v)
	}
	if v, err := readObj.Index("", true); err != ErrNotFound {
		t.Fatal(err)
	} else if v != nil {
		t.Fatal(v)
	}
	read, err := readObj.Value()
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(obj, read) {
		t.Fatal(read)
	}
}

func TestByteReadSeeker(t *testing.T) {
	data := make([]byte, 100)
	for i := range data {
		data[i] = byte(i)
	}
	r := bytes.NewReader(data)
	// bufio.NewReaderSize() has a internal minimum buffer size of 16.
	br, err := NewBufByteReadSeeker(r, 10)
	if err != nil {
		t.Fatal(err)
	}

	if b, err := br.ReadByte(); err != nil {
		t.Fatal(err)
	} else if b != 0 {
		t.Fatal(b)
	}

	// 15 is a corner case.
	// Seeks to the last buffered byte.
	if _, err := br.Seek(15, io.SeekStart); err != nil {
		t.Fatal(err)
	}

	var p = make([]byte, 2)
	if _, err := br.Read(p); err != nil {
		t.Fatal(err)
	} else if !bytes.Equal(p, []byte{15, 16}) {
		t.Fatal(p)
	}

}

func TestByteReadSeeker2(t *testing.T) {
	var buf bytes.Buffer
	err := WriteArray(&buf, []any{1, 2, 3}, nil)
	if err != nil {
		t.Fatal(err)
	}

	data := buf.Bytes()
	br, err := NewBufByteReadSeeker(bytes.NewReader(data), 5)
	if err != nil {
		t.Fatal(err)
	}

	ary, err := ReadArray(br)
	if err != nil {
		t.Fatal(err)
	}

	if v, err := ary.Index(0, true); err != nil {
		t.Fatal(err)
	} else if v != int64(1) {
		t.Fatal(v)
	}
}

func TestByteReadSeeker3(t *testing.T) {
	var buf bytes.Buffer
	var objValue = map[string]any{"3": ""}
	err := WriteObject(&buf, map[string]any{"": []any{objValue}}, nil)
	if err != nil {
		t.Fatal(err)
	}

	data := buf.Bytes()
	br, err := NewBufByteReadSeeker(bytes.NewReader(data), 5)
	if err != nil {
		t.Fatal(err)
	}

	obj, err := ReadObject(br)
	if err != nil {
		t.Fatal(err)
	}

	if v, err := obj.Index("", true); err != nil {
		t.Fatal(err)
	} else if !reflect.DeepEqual(v, []any{objValue}) {
		t.Fatal(v)
	}
}
