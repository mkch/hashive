# Hashive

[简体中文](README_CN.md)

Hashive is a single-file, read-only key-value database.

In simple terms, a Hashive database is essentially a `map[string]any` stored in a file, allowing for fast lookups without the need to load the entire file into memory.

## Design Goals

1. Single-file Database

    A Hashive database is a single file that can be queried without needing to load the entire file into memory.

2. Lightweight

    No additional configuration steps are required; it works out of the box. It does not significantly increase the complexity of the entire system.

3. Speed

    Hashive aims to achieve query speeds comparable to (or faster than) other databases.

4. Key-Value Query

    Hashive only stores key-value data. Values are queried using string-type keys.

5. Read-Only

    Hashive is optimized for read-only operations. More complex and slower write operations can be traded off for faster query speeds. In practice, a Hashive database is typically written (generated) only once.

It is evident that the first three design goals highly overlap with those of the SQLite database. However, SQLite is not a key-value database nor a read-only, and it lacks optimizations for key-value queries and read-only datasets. The third and fourth points highly overlap with hash tables, but hash tables require loading the entire dataset into memory to be usable. Hashive aims to find a balance between SQLite and in-memory hash tables, specifically optimized for read-only datasets.

## Features

1. Performance

    In specific scenarios, Hashive's query speed is 5 times faster than that of an SQLite3 database with the same content.

    Benchmark(3 random queries):

    ```text
    Hashive    110955   10112 ns/op   568 B/op    21 allocs/op
    SQLite     22893    51927 ns/op   1760 B/op   57 allocs/op
    ```

2. Direct Storage of Any Go Value

    In addition to commonly used types such as integers, floating-point numbers, strings, arrays, and associative objects (hash maps), any type supported by `encoding/gob` can be stored as a value.

## Example

```go
package hashive_test

import (
    "fmt"
    "os"

    "github.com/mkch/hashive"
)

type Description struct {
    Content string
}

func Example() {
    value := map[string]any{
        "Key1": 123,
        "Key2": "456",
        "Owners": []any{
            map[string]any{
                "Name": "John",
                "Age":  28,
            },
            map[string]any{
                "Name": "Joe",
                "Age":  29,
                "Addr": "abc street",
            },
        },
        "Description": Description{"description here"},
    }

    const DB_FILE = "testdata/db"
    // Write
    err := hashive.WriteFile(DB_FILE, value)
    if err != nil {
        panic(err)
    }
    defer os.Remove(DB_FILE)

    // Read
    db, close, err := hashive.Open(DB_FILE, -1)
    if err != nil {
        panic(err)
    }
    defer close()

    v1, _ := db.Query("Key1") // db.Key1
    fmt.Println(v1)
    v2, _ := db.Query("Key2") // db.Key2
    fmt.Println(v2)
    var desc Description
    err = db.QueryGob(&desc, "Description") // db.Description
    fmt.Println(desc)
    name, _ := db.Query("Owners", "0", "Name") // db.Owners[0].Name
    fmt.Println(name)
    addr, _ := db.Query("Owners", "1", "Addr") // db.Owners[1].Addr
    fmt.Println(addr)

    // Output:
    // 123
    // 456
    // {description here}
    // John
    // abc street
}
```
