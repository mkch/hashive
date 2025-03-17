package impl

import (
	"bytes"
	"encoding/gob"
	"io"
)

type writerWrapper struct {
	io.Writer
}

func (w *writerWrapper) Write(p []byte) (n int, err error) {
	return w.Writer.Write(p)
}

type byteReaderWrapper struct {
	io.Reader
}

func (r *byteReaderWrapper) Read(p []byte) (n int, err error) {
	return r.Reader.Read(p)
}

func (r *byteReaderWrapper) ReadByte() (b byte, err error) {
	if br, ok := r.Reader.(io.ByteReader); ok {
		return br.ReadByte()
	}
	var buf [1]byte
	_, err = r.Read(buf[:])
	if err == nil {
		b = buf[0]
	}
	return
}

type GobEncoder func(v any) GobValue
type GobDecoder func(gob GobValue, v any) error

func NewGobEncoder() GobEncoder {
	var gobWriterWrapper writerWrapper
	var gobEncoder = gob.NewEncoder(&gobWriterWrapper)

	return func(v any) GobValue {
		var buf bytes.Buffer
		gobWriterWrapper.Writer = &buf
		err := gobEncoder.Encode(v)
		if err != nil {
			panic(err)
		}
		return GobValue(buf.Bytes())
	}
}

func NewGobDecoder() GobDecoder {
	var gobReaderWrapper byteReaderWrapper
	var gobDecoder = gob.NewDecoder(&gobReaderWrapper)

	return func(gob GobValue, v any) error {
		gobReaderWrapper.Reader = bytes.NewBuffer(gob)
		return gobDecoder.Decode(v)
	}
}
