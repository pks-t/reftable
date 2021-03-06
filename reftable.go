/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

package reftable

import (
	"io"
	"os"
)

type fileBlockSource struct {
	f  *os.File
	sz uint64
}

// NewFileBlockSource opens a file on local disk as a BlockSource
func NewFileBlockSource(name string) (BlockSource, error) {
	f, err := os.Open(name)
	if err != nil {
		return nil, err
	}

	fi, err := f.Stat()
	if err != nil {
		return nil, err
	}

	return &fileBlockSource{f, uint64(fi.Size())}, nil
}
func (bs *fileBlockSource) Size() uint64 {
	return bs.sz
}

func (bs *fileBlockSource) ReadBlock(off uint64, size int) ([]byte, error) {
	if off >= bs.sz {
		return nil, io.EOF
	}
	if off+uint64(size) > bs.sz {
		size = int(bs.sz - off)
	}
	b := make([]byte, size)
	n, err := bs.f.ReadAt(b, int64(off))
	if err != nil {
		return nil, err
	}

	return b[:n], nil
}

func (bs *fileBlockSource) Close() error {
	return bs.f.Close()
}

// ReadRef reads a ref record by name.
func ReadRef(tab Table, name string) (*RefRecord, error) {
	it, err := tab.SeekRef(name)
	if err != nil {
		return nil, err
	}

	var ref RefRecord
	ok, err := it.NextRef(&ref)
	if err != nil || !ok {
		return nil, err
	}

	if ref.RefName != name {
		return nil, nil
	}

	return &ref, nil
}

// ReadRef reads the most recent log record of a certain ref.
func ReadLogAt(tab Table, name string, updateIndex uint64) (*LogRecord, error) {
	it, err := tab.SeekLog(name, updateIndex)
	if err != nil {
		return nil, err
	}

	var rec LogRecord
	ok, err := it.NextLog(&rec)
	if err != nil || !ok {
		return nil, err
	}

	if rec.RefName != name {
		return nil, nil
	}

	return &rec, nil
}
