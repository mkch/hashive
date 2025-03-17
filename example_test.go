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
