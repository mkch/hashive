package hashive

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math"
	"os"
	"strconv"
	"strings"

	"github.com/mkch/hashive/internal/impl"
)

const fileSignature = "hashive\x00"

// Write encodes value into Hashive format recursively and writes it to w.
//   - All singed integers are stored as int64.
//   - All unsigned integers are stored as uint64.
//   - Both float32 and float64 are stored as float64.
//   - bool, string and []byte are stored as is.
//   - []any is stored as array.
//   - map[string]any is stored as associated object.
//   - All the others types are stored as gob encoded binary data.
func Write(w io.Writer, value any) (err error) {
	buffered := bufio.NewWriter(w)
	defer func() {
		errFlush := buffered.Flush()
		if err == nil {
			err = errFlush
		}
	}()

	// Write magic number
	if _, err = buffered.WriteString(fileSignature); err != nil {
		return
	}

	gobEncoder := impl.NewGobEncoder()
	return impl.WriteValue(buffered, value, gobEncoder)
}

func writeFile(filename string, callback func(f *os.File) error) (err error) {
	f, err := os.OpenFile(filename, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0666)
	if err != nil {
		return
	}
	defer func() {
		errClose := f.Close()
		if err == nil {
			err = errClose
		}
	}()
	return callback(f)
}

// WriteFile is like [Write] but writes value to a file.
// The file will be overwritten if exists.
func WriteFile(filename string, value any) (err error) {
	return writeFile(filename, func(f *os.File) error {
		return Write(f, value)
	})
}

// WriteJSON decodes the next JSON-encoded value from jsonInput,
// and then writes the decoded value with [Write].
func WriteJSON(w io.Writer, jsonInput io.Reader) (err error) {
	var v any
	if err = json.NewDecoder(jsonInput).Decode(&v); err != nil {
		return
	}
	return Write(w, v)
}

// WriteFileJSON is like [WriteJSON] but writes the decoded value to a file.
// The file will be overwritten if exists.
func WriteFileJSON(filename string, jsonInput io.Reader) (err error) {
	return writeFile(filename, func(f *os.File) error {
		return WriteJSON(f, jsonInput)
	})
}

// WriteJSONString the next JSON-encoded value from jsonString,
// and then writes the decoded value with [Write].
func WriteJSONString(w io.Writer, jsonString string) (err error) {
	return WriteJSON(w, strings.NewReader(jsonString))
}

// WriteFileJSONString is like [WriteJSONString] but writes the decoded value to a file.
// The file will be overwritten if exists.
func WriteFileJSONString(filename string, jsonString string) (err error) {
	return writeFile(filename, func(f *os.File) error {
		return WriteJSONString(f, jsonString)
	})
}

// ErrNotFound is returned by [Hashive.Query] and [Hashive.QueryGob]
// when no matching value is found.
var ErrNotFound = impl.ErrNotFound

// Hashive is the Hashive instance.
type Hashive struct {
	r          impl.ByteReadSeeker
	ary        *impl.Array
	obj        *impl.Object
	gobDecoder func(gob impl.GobValue, v any) error
}

const defaultBufferSize = 1024

// Open opens the Hashive database denoted by filename.
// The returned close function can be used to close the database file after use.
// See [New] for more details.
func Open(filename string, readBufferSize int) (h *Hashive, close func() error, err error) {
	f, err := os.Open(filename)
	if err != nil {
		return
	}
	close = f.Close

	h, err = New(f, readBufferSize)
	return
}

// New creates a Hashive instance from r.
//
// If readBufferSize < 0, a reasonable default will be used.
func New(r io.ReadSeeker, readBufferSize int) (h *Hashive, err error) {
	if readBufferSize < 0 {
		readBufferSize = defaultBufferSize
	}
	reader, err := impl.NewBufByteReadSeeker(r, readBufferSize)
	if err != nil {
		return
	}
	signature := make([]byte, len(fileSignature))
	if _, err = io.ReadFull(reader, signature); err != nil {
		return
	}
	if sig := string(signature); sig != fileSignature {
		err = fmt.Errorf("invalid signature %v", sig)
		return
	}

	var ary *impl.Array
	var obj *impl.Object
	obj, err = impl.ReadObject(reader)
	var typeErr *impl.TypeError
	if errors.As(err, &typeErr) {
		if _, err = reader.Seek(int64(len(fileSignature)), io.SeekStart); err != nil {
			return
		}
		ary, err = impl.ReadArray(reader)
		if !errors.As(err, &typeErr) {
			return
		}
	}

	return &Hashive{
		r:          reader,
		ary:        ary,
		obj:        obj,
		gobDecoder: impl.NewGobDecoder(),
	}, nil
}

// QueryGob queries a gob encoded value mapped by the path.
// [ErrNotFound] will be returned if the path does not map to any value
// or the type of the value is not a gob encoded value.
//
// For the meaning of argument path, see [Hashive.Query].
func (h *Hashive) QueryGob(v any, path ...string) (err error) {
	value, err := h.Query(path...)
	if err != nil {
		return
	}
	if gob, ok := value.(impl.GobValue); ok {
		err = h.gobDecoder(gob, v)
	} else {
		err = ErrNotFound
	}
	return
}

// Query queries a value mapped by the path.
// [ErrNotFound] will be returned if the path does not map to any value.
//
// The path argument is a sequence of map key or array index:
//
//	h.Query("key1", "key2", "1", "key3")
//
// is analogous to
//
//	h["key1"]["key2"][1]["key3"]
//
// Empty path maps to the entire value(a map[string]any or []any).
func (h *Hashive) Query(path ...string) (v any, err error) {
	if len(path) == 0 {
		if _, err = h.r.Seek(int64(len(fileSignature)), io.SeekStart); err != nil {
			return
		}
		return impl.ReadValue(h.r, true)
	}
	if h.obj != nil {
		return queryObject(path, h.obj)
	} else if h.ary != nil {
		return queryArray(path, h.ary)
	}
	return nil, ErrNotFound
}

func queryObject(path []string, obj *impl.Object) (v any, err error) {
	value, err := obj.Index(path[0], len(path) == 1)
	if err != nil {
		return
	}
	if len(path) == 1 {
		return value, err
	} else if obj, ok := value.(*impl.Object); ok {
		return queryObject(path[1:], obj)
	} else if ary, ok := value.(*impl.Array); ok {
		return queryArray(path[1:], ary)
	}
	return nil, ErrNotFound
}

func queryArray(path []string, ary *impl.Array) (v any, err error) {
	index, err := strconv.ParseUint(path[0], 0, 64)
	if err != nil {
		return
	}
	if index > math.MaxInt {
		err = fmt.Errorf("invalid index %v", index)
		return
	}

	value, err := ary.Index(int(index), len(path) == 1)
	if err != nil {
		return
	}
	if len(path) == 1 {
		return value, err
	} else if obj, ok := value.(*impl.Object); ok {
		return queryObject(path[1:], obj)
	} else if ary, ok := value.(*impl.Array); ok {
		return queryArray(path[1:], ary)
	}
	return nil, ErrNotFound
}
