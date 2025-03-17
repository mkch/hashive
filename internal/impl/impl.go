package impl

import (
	"bufio"
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"hash/fnv"
	"io"
	"math"
)

// typeMarker is a byte that precedes every typed Hashive value.
type typeMarker byte

// Type returns the type of t.
func (t typeMarker) Type() typ {
	return typ(t & 0x0F)
}

// OffsetSize returns the offset size of t.
// Offset size is only exists in [typeArray] and [typeObject].
func (t typeMarker) OffsetSize() byte {
	return byte(t >> 4)
}

func newTypeMarker(t typ, size byte) typeMarker {
	return typeMarker(t) | typeMarker(size<<4)
}

type typ byte

const (
	typeNull   typ = iota // JSON null or go nil
	typeInt               // All signed integers
	typeUint              // All unsigned integers
	typeBool              // bool
	typeString            // string
	typeFloat             // float64
	typeBinary            // []byte
	typeGob               // gob encoded go values
	typeArray             // []any
	typeObject            // map[string]any
)

// ByteWriter is the interface that groups the io.Writer and io.ByteWriter.
type ByteWriter interface {
	io.Writer
	io.ByteWriter
}

// ByteReadSeeker is the interface that groups the io.ReadSeeker and io.ByteReader.
type ByteReadSeeker interface {
	io.ReadSeeker
	io.ByteReader
}

type bufByteReadSeeker struct {
	r         io.ReadSeeker
	buf       *bufio.Reader
	bufOffset int64 // the seek offset of r when r is wrapped into buf
	bufRead   int   // the number of bytes read from buf since wrapping
}

// NewBufByteReadSeeker wraps r in a [bufio.Reader] and implements [io.ByteReader].
// Argument bufferSize is passed to [bufio.NewReaderSize] to create the buffered reader if not zero.
func NewBufByteReadSeeker(r io.ReadSeeker, bufferSize int) (brs ByteReadSeeker, err error) {
	if bufferSize == 0 {
		return &byteReadSeeker{ReadSeeker: r}, nil
	}
	offset, err := r.Seek(0, io.SeekCurrent)
	if err != nil {
		return
	}
	return &bufByteReadSeeker{
		r:         r,
		buf:       bufio.NewReaderSize(r, bufferSize),
		bufOffset: offset,
	}, nil
}

func (r *bufByteReadSeeker) Read(p []byte) (n int, err error) {
	n, err = r.buf.Read(p)
	r.bufRead += n
	return
}

func (r *bufByteReadSeeker) Seek(offset int64, whence int) (n int64, err error) {
	current := r.bufOffset + int64(r.bufRead)
	if whence == io.SeekCurrent {
		if offset == 0 {
			// Tells the current offset.
			return current, nil
		}
		whence = io.SeekStart
		offset += current
	}
	n, err = r.r.Seek(offset, whence)
	if err == nil {
		advanced := n - current
		if advanced == 0 {
			return
		}
		r.buf.Reset(r.r)
		r.bufOffset = n
		r.bufRead = 0
	}
	return
}

func (r *bufByteReadSeeker) ReadByte() (b byte, err error) {
	b, err = r.buf.ReadByte()
	if err == nil {
		r.bufRead++
	}
	return
}

var littleEndian = binary.LittleEndian

// fixedUintSize returns the minimum byte size to store n.
func fixedUintSize(n uint64) byte {
	if n > 0xFF_FF_FF_FF_FF_FF_FF {
		return 8
	} else if n > 0xFF_FF_FF_FF_FF_FF {
		return 7
	} else if n > 0xFF_FF_FF_FF_FF {
		return 6
	} else if n > 0xFF_FF_FF_FF {
		return 5
	} else if n > 0xFF_FF_FF {
		return 4
	} else if n > 0xFF_FF {
		return 3
	} else if n > 0xFF {
		return 2
	} else {
		return 1
	}
}

type byteReadSeeker struct {
	io.ReadSeeker
	buf [1]byte
}

func (r *byteReadSeeker) ReadByte() (b byte, err error) {
	_, err = r.Read(r.buf[:])
	b = r.buf[0]
	return
}

// writeFixedUint converts n to a byte sequence and write it to w.
// If n doesn't fit the size, it will be truncated.
func writeFixedUint(w io.Writer, n uint64, size byte) (err error) {
	var buf [8]byte // size of uint64
	switch size {
	case 1:
		buf[0] = byte(n)
	case 2:
		littleEndian.PutUint16(buf[:], uint16(n))
	case 3, 4:
		littleEndian.PutUint32(buf[:], uint32(n))
	case 5, 6, 7, 8:
		littleEndian.PutUint64(buf[:], n)
	default:
		err = fmt.Errorf("invalid size %v", size)
		return
	}
	_, err = w.Write(buf[:size])
	return
}

// readFixedUint reads a byte sequence from r and convert it to a unsigned integer.
func readFixedUint(r ByteReadSeeker, size byte) (n uint64, err error) {
	var buf [8]byte // size of uint64
	if _, err = io.ReadFull(r, buf[:size]); err != nil {
		return
	}
	switch size {
	case 1:
		n = uint64(buf[0])
	case 2:
		n = uint64(littleEndian.Uint16(buf[:]))
	case 3, 4:
		n = uint64(littleEndian.Uint32(buf[:]))
	case 5, 6, 7, 8:
		n = littleEndian.Uint64(buf[:])
	default:
		err = fmt.Errorf("invalid size %v", size)
		return
	}
	return
}

// writeUintValue write n to w without the type mark.
// Argument n is encoded with a variable-length encoding.
func writeUintValue(w io.Writer, n uint64) (err error) {
	var buf = make([]byte, 9)
	if n <= math.MaxInt8 {
		buf[0] = byte(n)
		buf = buf[:1]
	} else if n <= 0xFF {
		buf[0] = ^byte(1) + 1 // -1
		buf[1] = byte(n)
		buf = buf[:2]
	} else if n <= 0xFF_FF {
		buf[0] = ^byte(2) + 1 // -2
		littleEndian.PutUint16(buf[1:], uint16(n))
		buf = buf[:3]
	} else if n <= 0xFF_FF_FF {
		buf[0] = ^byte(3) + 1 // -3
		littleEndian.PutUint32(buf[1:], uint32(n))
		buf = buf[:4]
	} else if n <= 0xFF_FF_FF_FF {
		buf[0] = ^byte(4) + 1 // -4
		littleEndian.PutUint32(buf[1:], uint32(n))
		buf = buf[:5]
	} else if n <= 0xFF_FF_FF_FF_FF {
		buf[0] = ^byte(5) + 1 // -5
		littleEndian.PutUint64(buf[1:], n)
		buf = buf[:6]
	} else if n <= 0xFF_FF_FF_FF_FF_FF {
		buf[0] = ^byte(6) + 1 // -6
		littleEndian.PutUint64(buf[1:], n)
		buf = buf[:7]
	} else if n <= 0xFF_FF_FF_FF_FF_FF_FF {
		buf[0] = ^byte(7) + 1 // -7
		littleEndian.PutUint64(buf[1:], n)
		buf = buf[:8]
	} else {
		buf[0] = ^byte(8) + 1 // -8
		littleEndian.PutUint64(buf[1:], n)
		buf = buf[:9]
	}
	_, err = w.Write(buf)
	return
}

// readUintValue reads a variable-length encoded unsigned integer form r
// after the type mark.
func readUintValue(r ByteReadSeeker) (n uint64, err error) {
	var b0 byte
	if b0, err = r.ReadByte(); err != nil {
		return
	}
	if b0 <= math.MaxInt8 {
		return uint64(b0), nil
	} else {
		length := ^(b0 - 1) // length = -b0
		return readFixedUint(r, length)
	}
}

// WriteUint writes n with a variable-length encoding.
func WriteUint(w ByteWriter, n uint64) (err error) {
	if err = w.WriteByte(byte(typeUint)); err != nil {
		return
	}
	err = writeUintValue(w, n)
	return
}

// ReadUint reads a variable-length encoded unsigned integer.
func ReadUint(r ByteReadSeeker) (n uint64, err error) {
	b, err := r.ReadByte()
	if err != nil {
		return
	}
	if t := typeMarker(b).Type(); t != typeUint {
		err = fmt.Errorf("failed to read uint: invalid type %v", t)
		return
	}
	n, err = readUintValue(r)
	return
}

// WriteBool writes b to w.
func WriteBool(w ByteWriter, b bool) (err error) {
	var n uint64
	if b {
		n = 1
	}
	if err = w.WriteByte(byte(typeBool)); err != nil {
		return
	}
	err = writeUintValue(w, n)
	return
}

// readBoolValue reads a bool from r after the type mark.
func readBoolValue(r ByteReadSeeker) (b bool, err error) {
	n, err := readUintValue(r)
	if err != nil {
		return
	}

	if n == 0 {
		b = false
	} else if n == 1 {
		b = true
	} else {
		err = fmt.Errorf("failed to read bool: invalid value %v", n)
		return
	}
	return
}

// ReadBool reads a bool from r.
func ReadBool(r ByteReadSeeker) (b bool, err error) {
	tb, err := r.ReadByte()
	if err != nil {
		return
	}
	if t := typeMarker(tb).Type(); t != typeBool {
		err = fmt.Errorf("failed to read bool: invalid type %v", t)
		return
	}
	return readBoolValue(r)
}

// WriteInt writes a signed integer to w.
func WriteInt(w ByteWriter, n int64) (err error) {
	if err = w.WriteByte(byte(typeInt)); err != nil {
		return
	}
	err = writeUintValue(w, int2Uint(n))
	return
}

func int2Uint(n int64) uint64 {
	// A signed integer, i, is encoded within an unsigned integer, u.
	// Within u, bits 1 upward contain the value;
	// bit 0 says whether they should be complemented upon receipt.
	//
	// The low bit is therefore analogous to a sign bit, but making it
	// the complement bit instead guarantees that the largest negative
	// integer is not a special case.
	var u uint64
	if n < 0 {
		u = (^uint64(n) << 1) | 1
	} else {
		u = (uint64(n) << 1)
	}
	return u
}

func uint2Int(u uint64) int64 {
	// See int2Uint.
	if u&1 == 1 {
		return int64(^(u >> 1) | 0x80000000)
	} else {
		return int64(u >> 1)
	}
}

// readIntValue reads a singed integer from r after the type mark.
func readIntValue(r ByteReadSeeker) (n int64, err error) {
	u, err := readUintValue(r)
	if err != nil {
		return
	}
	n = uint2Int(u)
	return
}

// ReadInt reads a signed integer from r.
func ReadInt(r ByteReadSeeker) (n int64, err error) {
	b, err := r.ReadByte()
	if err != nil {
		return
	}
	if t := typeMarker(b).Type(); t != typeInt {
		err = fmt.Errorf("failed to read int: invalid type %v", t)
		return
	}
	n, err = readIntValue(r)
	return
}

// reverseBytes reverse the 8 bytes in n.
func reverseBytes(n uint64) uint64 {
	return ((n >> 56) & 0xFF) |
		((n >> 40) & (0xFF << 8)) |
		((n >> 24) & (0xFF << 16)) |
		((n >> 8) & (0xFF << 24)) |
		((n & 0xFF) << 56) |
		((n & 0xFF00) << 40) |
		((n & 0xFF0000) << 24) |
		((n & 0xFF000000) << 8)
}

// WriteFloat writes f to w.
func WriteFloat(w ByteWriter, f float64) (err error) {
	// Floating-point numbers are always sent as a representation of a float64 value.
	// That value is converted to a uint64 using math.Float64bits.
	// The uint64 is then byte-reversed and sent as a regular unsigned integer.
	if err = w.WriteByte(byte(typeFloat)); err != nil {
		return
	}
	u := reverseBytes(math.Float64bits(f))
	err = writeUintValue(w, u)
	return
}

// readFloatValue reads a float point number from r after the type mark.
func readFloatValue(r ByteReadSeeker) (f float64, err error) {
	n, err := readUintValue(r)
	if err != nil {
		return
	}
	f = math.Float64frombits(reverseBytes(n))
	return
}

// ReadFloat reads a float point number form r.
func ReadFloat(r ByteReadSeeker) (f float64, err error) {
	tb, err := r.ReadByte()
	if err != nil {
		return
	}
	if t := typeMarker(tb).Type(); t != typeFloat {
		err = fmt.Errorf("failed to read float: invalid type %v", t)
		return
	}
	return readFloatValue(r)
}

// writeBinary writes a byte sequence([]byte) to w with type t.
// The argument t should be [typeString], [typeBinary] or [typeGob].
func writeBinary(w ByteWriter, t typ, p []byte) (err error) {
	if err = w.WriteByte(byte(t)); err != nil {
		return
	}
	if err = writeUintValue(w, uint64(len(p))); err == nil {
		_, err = w.Write(p)
	}
	return
}

// writeBinaryValue writes p to w without a type mark.
func writeBinaryValue(w io.Writer, p []byte) (err error) {
	if err = writeUintValue(w, uint64(len(p))); err == nil {
		_, err = w.Write(p)
	}
	return
}

// readBinaryValue reads a byte sequence form r after the type mark.
func readBinaryValue(r ByteReadSeeker) (p []byte, err error) {
	length, err := readUintValue(r)
	if err != nil {
		return
	}
	if length > math.MaxInt {
		err = fmt.Errorf("failed to read binary: invalid length %v", length)
		return
	}
	p = make([]byte, length)
	_, err = io.ReadFull(r, p)
	return
}

// readBinary reads a [typeString], [typeBinary] or [typeGob] from r.
func readBinary(r ByteReadSeeker, t typ) (p []byte, err error) {
	tb, err := r.ReadByte()
	if err != nil {
		return
	}
	if destT := typeMarker(tb).Type(); destT != t {
		err = fmt.Errorf("failed to read binary: invalid type %v", destT)
		return
	}
	return readBinaryValue(r)
}

// WriteBinary writes a byte sequence to w.
func WriteBinary(w ByteWriter, p []byte) (err error) {
	return writeBinary(w, typeBinary, p)
}

// ReadBinary reads a byte sequence form r.
func ReadBinary(r ByteReadSeeker) (p []byte, err error) {
	return readBinary(r, typeBinary)
}

// readStringValue reads a [typeString] from r after the type mark.
func readStringValue(r ByteReadSeeker) (s string, err error) {
	p, err := readBinaryValue(r)
	if err != nil {
		return
	}
	s = string(p)
	return
}

// ReadString reads a string from r.
func ReadString(r ByteReadSeeker) (s string, err error) {
	p, err := readBinary(r, typeString)
	if err != nil {
		return
	}
	s = string(p)
	return
}

// WriteString writes a string to w.
func WriteString(w ByteWriter, s string) (err error) {
	return writeBinary(w, typeString, []byte(s))
}

// WriteGob writes the gob encoding of v to w.
func WriteGob(w ByteWriter, v any, encode GobEncoder) (err error) {
	return writeBinary(w, typeGob, encode(v))
}

// readGobValue reads a GobValue from r.
func readGobValue(r ByteReadSeeker) (gob GobValue, err error) {
	p, err := readBinaryValue(r)
	if err != nil {
		return
	}
	gob = GobValue(p)
	return
}

// GobValue is the gob encoded value.
// Use [GobGobDecoder] to decode the value.
type GobValue []byte

// ReadGob reads gob encoded value from r.
func ReadGob(r ByteReadSeeker) (gob GobValue, err error) {
	p, err := readBinary(r, typeGob)
	if err != nil {
		return
	}
	gob = GobValue(p)
	return
}

// WriteNull writes a null.
// JSON null and go nil are encoded as null.
func WriteNull(w ByteWriter) (err error) {
	return w.WriteByte(byte(typeNull))
}

// WriteValue writes v to w.
//   - All singed integers are stored as int64.
//   - All unsigned integers are stored as uint64.
//   - Both float32 and float64 are stored as float64.
//   - bool, string and []byte are stored as is.
//   - []any is stored as array.
//   - map[string]any is stored as associated object.
//   - All the others types are stored as gob encoded binary data.
func WriteValue(w ByteWriter, v any, gobEncoder GobEncoder) (err error) {
	switch value := v.(type) {
	case nil:
		return WriteNull(w)
	case int8:
		return WriteUint(w, uint64(value))
	case uint8:
		return WriteInt(w, int64(value))
	case int16:
		return WriteInt(w, int64(value))
	case uint16:
		return WriteUint(w, uint64(value))
	case int32:
		return WriteInt(w, int64(value))
	case uint32:
		return WriteUint(w, uint64(value))
	case int64:
		return WriteInt(w, int64(value))
	case uint64:
		return WriteUint(w, uint64(value))
	case int:
		return WriteInt(w, int64(value))
	case uint:
		return WriteUint(w, uint64(value))
	case bool:
		return WriteBool(w, value)
	case string:
		return WriteString(w, value)
	case float32:
		return WriteFloat(w, float64(value))
	case float64:
		return WriteFloat(w, value)
	case []byte:
		return WriteBinary(w, value)
	case []any:
		return WriteArray(w, value, gobEncoder)
	case map[string]any:
		return WriteObject(w, value, gobEncoder)
	default:
		return WriteGob(w, v, gobEncoder)
	}
}

// WriteArray writes an array to w.
func WriteArray(w io.Writer, array []any, gobEncoder GobEncoder) (err error) {
	var offsets = make([]int, len(array))
	var data bytes.Buffer
	for i, elem := range array {
		offsets[i] = data.Len()
		WriteValue(&data, elem, gobEncoder)
	}

	var maxOffset = 0
	if len(offsets) > 0 {
		maxOffset = offsets[len(offsets)-1]
	}
	offsetSize := fixedUintSize(uint64(maxOffset))
	// offsetSize must be large enough to hold the max offset plus the size of offset section.
	for offsetSize < fixedUintSize(uint64(maxOffset+len(array)*int(offsetSize))) {
		offsetSize *= 2
		if offsetSize > 8 {
			err = fmt.Errorf("invalid offset size %v", offsetSize)
			return
		}
	}

	// Fix offsets
	delta := len(array) * int(offsetSize)
	for i := range offsets {
		offsets[i] += delta
	}

	var buf bytes.Buffer
	buf.WriteByte(byte(newTypeMarker(typeArray, offsetSize)))
	writeFixedUint(&buf, uint64(len(array)), offsetSize)
	for _, offset := range offsets {
		writeFixedUint(&buf, uint64(offset), offsetSize)
	}
	io.Copy(&buf, &data)

	_, err = io.Copy(w, &buf)
	return
}

// ReadValue reads a value from r.
// See [WriteValue] for the the type of v.
// If recursive is false, arrays and maps are returned as [Array] and [Object],
// otherwise they are returned as []any and map[string]any.
func ReadValue(r ByteReadSeeker, recursive bool) (v any, err error) {
	tb, err := r.ReadByte()
	if err != nil {
		return
	}
	mt := typeMarker(tb)
	switch t := mt.Type(); t {
	case typeNull:
		// NOP
	case typeInt:
		var n int64
		if n, err = readIntValue(r); err != nil {
			return
		}
		v = n
	case typeUint:
		var n uint64
		if n, err = readUintValue(r); err != nil {
			return
		}
		v = n
	case typeBool:
		var b bool
		if b, err = readBoolValue(r); err != nil {
			return
		}
		v = b
	case typeString:
		var s string
		if s, err = readStringValue(r); err != nil {
			return
		}
		v = s
	case typeFloat:
		var f float64
		if f, err = readFloatValue(r); err != nil {
			return
		}
		v = f
	case typeBinary:
		var b []byte
		if b, err = readBinaryValue(r); err != nil {
			return
		}
		v = b
	case typeGob:
		var g GobValue
		if g, err = readGobValue(r); err != nil {
			return
		}
		v = g
	case typeArray:
		var array *Array
		if array, err = readArrayValue(r, mt.OffsetSize()); err != nil {
			return
		}
		if !recursive {
			v = array
			break
		}
		var value []any
		if value, err = array.Value(); err != nil {
			return
		}
		v = value
	case typeObject:
		var obj *Object
		if obj, err = readObjectValue(r, mt.OffsetSize()); err != nil {
			return
		}
		if !recursive {
			v = obj
			break
		}
		var value map[string]any
		if value, err = obj.Value(); err != nil {
			return
		}
		v = value
	default:
		err = fmt.Errorf("failed to read value: invalid type %v", t)
	}
	return
}

// BoundsError is returned when an out-of-bounds error occurs in [Array.Index].
type BoundsError struct {
	Length, Index int
}

func (err *BoundsError) Error() string {
	return fmt.Sprintf("array index out of range, %v of %v", err.Index, err.Length)
}

// Array is an descriptor of []any read from a stream.
type Array struct {
	r          ByteReadSeeker
	pos        int64
	length     int
	offsetSize byte
}

// Len returns the length of array.
func (array *Array) Len() int {
	return array.length
}

// Index returns the ith element of array.
// If recursive is false, arrays and maps are returned as [Array] and [Object],
// otherwise they are returned as []any and map[string]any.
func (array *Array) Index(i int, recursive bool) (v any, err error) {
	if i < 0 || i+1 > array.length {
		err = &BoundsError{Length: array.length, Index: i}
		return
	}
	offsetPos := int64(array.offsetSize) * int64(i)
	_, err = array.r.Seek(array.pos+offsetPos, io.SeekStart)
	if err != nil {
		return
	}
	offset, err := readFixedUint(array.r, array.offsetSize)
	if err != nil {
		return
	}
	if offset > math.MaxInt64 {
		err = fmt.Errorf("invalid offset %v", offset)
		return
	}
	_, err = array.r.Seek(array.pos+int64(offset), io.SeekStart)
	if err != nil {
		return
	}
	return ReadValue(array.r, recursive)
}

// Value reads and returns the content of array.
func (array *Array) Value() (v []any, err error) {
	v = make([]any, 0, array.length)
	for i := range array.length {
		offsetPos := int64(array.offsetSize) * int64(i)
		if _, err = array.r.Seek(array.pos+offsetPos, io.SeekStart); err != nil {
			return
		}
		var offset uint64
		offset, err = readFixedUint(array.r, array.offsetSize)
		if err != nil {
			return
		}
		if _, err = array.r.Seek(array.pos+int64(offset), io.SeekStart); err != nil {
			return
		}
		var elem any
		elem, err = ReadValue(array.r, true)
		if err != nil {
			return
		}
		v = append(v, elem)
	}
	return
}

// readArrayValue reads an Array form r after the type mark.
func readArrayValue(r ByteReadSeeker, offsetSize byte) (array *Array, err error) {
	length, err := readFixedUint(r, offsetSize)
	if err != nil {
		return
	}
	if length > math.MaxInt {
		err = fmt.Errorf("failed to read array: invalid length %v", length)
		return
	}

	pos, err := r.Seek(0, io.SeekCurrent)
	if err != nil {
		return
	}
	array = &Array{
		r:          r,
		pos:        pos,
		length:     int(length),
		offsetSize: offsetSize,
	}
	return
}

// TypeError is returned when an unexpected type is encountered when reading.
type TypeError struct {
	t typ
}

func (err *TypeError) Error() string {
	return fmt.Sprintf("invalid type %v", err.t)
}

// ReadArray reads an Array from r.
func ReadArray(r ByteReadSeeker) (array *Array, err error) {
	tb, err := r.ReadByte()
	if err != nil {
		return
	}
	tm := typeMarker(tb)
	if t := tm.Type(); t != typeArray {
		err = fmt.Errorf("failed to read array: invalid type %w", &TypeError{t})
		return
	}
	return readArrayValue(r, tm.OffsetSize())
}

func stringHash(s string) uint64 {
	h := fnv.New64a()
	io.WriteString(h, s)
	return h.Sum64()
}

type bucketKV struct {
	K string
	V any
}

// genBuckets is the Separate Chaining hash table algorithm.
func genBuckets(obj map[string]any, bucketCount int) (buckets [][]bucketKV, avgOverflow int) {
	buckets = make([][]bucketKV, bucketCount)
	for k, v := range obj {
		hash := stringHash(k)
		i := hash % uint64(bucketCount)
		buckets[i] = append(buckets[i], bucketKV{k, v})
	}
	var sumOverflow int
	var numOverflow int
	for _, b := range buckets {
		if overflow := len(b); overflow > 1 {
			numOverflow++
			sumOverflow += overflow
		}
	}
	if numOverflow > 0 {
		avgOverflow = sumOverflow / numOverflow
	}
	return
}

// WriteObject writes a map[string]any to w.
func WriteObject(w io.Writer, obj map[string]any, gobEncoder GobEncoder) (err error) {
	bucketCount := nearestPrime(len(obj) * 4 / 3)
	buckets, avgOverflow := genBuckets(obj, bucketCount)
	if avgOverflow > 5 {
		bucketCount = nearestPrime(max(bucketCount*4/3, bucketCount+1))
		buckets, _ = genBuckets(obj, bucketCount)
	}

	var bucketData bytes.Buffer
	var offsets = make([]int, bucketCount)
	for i, list := range buckets {
		offsets[i] = bucketData.Len()
		// List size
		writeUintValue(&bucketData, uint64(len(list)))
		// List data
		for _, bucket := range list {
			writeBinaryValue(&bucketData, []byte(bucket.K))
			var valueData bytes.Buffer
			WriteValue(&valueData, bucket.V, gobEncoder)
			// Used to skip value
			writeUintValue(&bucketData, uint64(valueData.Len()))
			io.Copy(&bucketData, &valueData)
		}
	}

	var maxOffset = 0
	if len(offsets) > 0 {
		maxOffset = offsets[len(offsets)-1]
	}
	var offsetSize = fixedUintSize(uint64(maxOffset))
	// offsetSize must be large enough to hold the max offset plus the size of offset section.
	for offsetSize < fixedUintSize(uint64(maxOffset+bucketCount*int(offsetSize))) {
		offsetSize *= 2
		if offsetSize > 8 {
			err = fmt.Errorf("invalid offset size %v", offsetSize)
			return
		}
	}

	// Fix offsets
	delta := bucketCount * int(offsetSize)
	for i := range offsets {
		offsets[i] += delta
	}

	var header bytes.Buffer
	header.WriteByte(byte(newTypeMarker(typeObject, offsetSize)))
	writeUintValue(&header, uint64(bucketCount))
	for _, offset := range offsets {
		writeFixedUint(&header, uint64(offset), offsetSize)
	}

	if _, err = io.Copy(w, &header); err == nil {
		_, err = io.Copy(w, &bucketData)
	}
	return
}

// ErrNotFound is returned when no value is associated with a key
// when indexing an map[string]any.
var ErrNotFound = errors.New("not found")

// Array is an descriptor of map[string]any read from a stream.
type Object struct {
	r           ByteReadSeeker
	pos         int64
	bucketCount uint64
	offsetSize  byte
}

// Value reads and returns the content of obj.
func (obj *Object) Value() (v map[string]any, err error) {
	v = make(map[string]any)
	for i := range obj.bucketCount {
		offsetPos := obj.pos + int64(i)*int64(obj.offsetSize)
		if _, err = obj.r.Seek(offsetPos, io.SeekStart); err != nil {
			return
		}
		var offset uint64
		offset, err = readFixedUint(obj.r, obj.offsetSize)
		if err != nil {
			return
		}
		if offset > math.MaxInt64 {
			err = fmt.Errorf("invalid offset %v", offset)
			return
		}
		_, err = obj.r.Seek(obj.pos+int64(offset), io.SeekStart)
		if err != nil {
			return
		}
		var listLen uint64
		listLen, err = readUintValue(obj.r)
		if err != nil {
			return
		}
		for range listLen {
			var key string
			if key, err = readStringValue(obj.r); err != nil {
				return
			}
			// Read value size
			if _, err = readUintValue(obj.r); err != nil {
				return
			}
			var value any
			if value, err = ReadValue(obj.r, true); err != nil {
				return
			}
			v[key] = value
		}
	}
	return
}

// Index returns the value associated with key. The returned error is [ErrNotFound]
// if no value is associated with key.
// See [Array.Index] for the meaning of recursive.
func (obj *Object) Index(key string, recursive bool) (v any, err error) {
	hash := stringHash(key)
	i := hash % obj.bucketCount
	offsetPos := obj.pos + int64(i)*int64(obj.offsetSize)
	if _, err = obj.r.Seek(offsetPos, io.SeekStart); err != nil {
		return
	}
	offset, err := readFixedUint(obj.r, obj.offsetSize)
	if err != nil {
		return
	}
	if offset > math.MaxInt64 {
		err = fmt.Errorf("invalid offset %v", offset)
		return
	}
	_, err = obj.r.Seek(obj.pos+int64(offset), io.SeekStart)
	if err != nil {
		return
	}
	listLen, err := readUintValue(obj.r)
	if err != nil {
		return
	}
	for range listLen {
		var bucketKey string
		if bucketKey, err = readStringValue(obj.r); err != nil {
			return
		}
		if key == bucketKey { // FOUND!
			// Read value size
			if _, err = readUintValue(obj.r); err != nil {
				return
			}
			return ReadValue(obj.r, recursive)
		}

		// Read value size
		var valueSize uint64
		if valueSize, err = readUintValue(obj.r); err != nil {
			return
		}
		// Skip value
		if _, err = obj.r.Seek(int64(valueSize), io.SeekCurrent); err != nil {
			return
		}
	}
	return nil, ErrNotFound
}

// readObjectValue reads a map[string]any from r after the type mark.
func readObjectValue(r ByteReadSeeker, offsetSize byte) (obj *Object, err error) {
	bucketCount, err := readUintValue(r)
	if err != nil {
		return
	}
	pos, err := r.Seek(0, io.SeekCurrent)
	if err != nil {
		return
	}
	obj = &Object{
		r:           r,
		pos:         pos,
		bucketCount: bucketCount,
		offsetSize:  offsetSize,
	}
	return
}

// ReadObject reads a map[string]any from r.
func ReadObject(r ByteReadSeeker) (obj *Object, err error) {
	tb, err := r.ReadByte()
	if err != nil {
		return
	}
	tm := typeMarker(tb)
	if t := tm.Type(); t != typeObject {
		err = fmt.Errorf("failed to read object: invalid type %v", t)
		return
	}
	return readObjectValue(r, tm.OffsetSize())
}
