package hashive_test

import (
	"bufio"
	"database/sql"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"testing"

	"github.com/mkch/hashive"

	_ "github.com/mattn/go-sqlite3"
)

const IEEE_OUI_URL = "https://standards-oui.ieee.org/"

var lineRegexp = regexp.MustCompile(`([0-9A-F]{6})\s+\(base 16\)\s+(.+)`)

const OUI_LIST = "testdata/oui.txt"
const HASHIVE_DB = "testdata/oui.hashive"
const SQLITE_DB = "testdata/oui.sqlite"

func downloadOUIList(filename string) (err error) {
	resp, err := http.Get(IEEE_OUI_URL)
	if err != nil {
		return
	}
	defer resp.Body.Close()
	f, err := os.OpenFile(filename, os.O_WRONLY|os.O_CREATE, 0666)
	if err != nil {
		return
	}
	defer f.Close()
	_, err = io.Copy(f, resp.Body)
	return
}

func genHashiveDB(filename string) (err error) {
	f, err := os.OpenFile(filename, os.O_WRONLY|os.O_CREATE, 0666)
	if err != nil {
		return
	}
	defer f.Close()

	list, err := os.Open(OUI_LIST)
	if err != nil {
		return
	}
	defer list.Close()

	var kvMap = make(map[string]any)
	scanner := bufio.NewScanner(list)
	for scanner.Scan() {
		if err = scanner.Err(); err != nil {
			return
		}
		m := lineRegexp.FindSubmatch(scanner.Bytes())
		if m == nil {
			continue
		}
		k := string(m[1])
		v := string(m[2])
		kvMap[k] = v // Keep last value of duplicated keys.
	}
	return hashive.WriteFile(HASHIVE_DB, kvMap)
}

func genSqliteDB(filename string) (err error) {
	db, err := sql.Open("sqlite3",
		(&url.URL{Scheme: "file",
			Path:     filepath.ToSlash(filename),
			RawQuery: "mode=rwc&_mutex=no",
			OmitHost: true,
		}).String())
	if err != nil {
		return
	}
	defer db.Close()

	if _, err = db.Exec(`CREATE TABLE IF NOT EXISTS oui (id PRIMARY KEY, company NOT NULL)`); err != nil {
		return
	}
	tx, err := db.Begin()
	if err != nil {
		return
	}
	defer tx.Rollback()

	// OR REPLACE: IEEE OUI has duplicated entries for historical reason.
	insert, err := tx.Prepare(`INSERT OR REPLACE INTO oui (id, company) VALUES(?,?)`)
	if err != nil {
		return
	}

	list, err := os.Open(OUI_LIST)
	if err != nil {
		return
	}
	defer list.Close()

	scanner := bufio.NewScanner(list)
	for scanner.Scan() {
		if err = scanner.Err(); err != nil {
			return
		}
		m := lineRegexp.FindSubmatch(scanner.Bytes())
		if m == nil {
			continue
		}
		id := string(m[1])
		company := string(m[2])
		if _, err = insert.Exec(id, company); err != nil {
			return fmt.Errorf("invalid data %v %v: %w", id, company, err)
		}
	}
	if err = tx.Commit(); err != nil {
		return
	}
	return db.Close()
}

type sqliteDB struct {
	db    *sql.DB
	query *sql.Stmt
}

func newSqliteDB(filename string) (*sqliteDB, error) {
	db, err := sql.Open("sqlite3",
		(&url.URL{
			Scheme:   "file",
			Path:     filepath.ToSlash(filename),
			RawQuery: "mode=ro&_mutex=no",
			OmitHost: true,
		}).String())
	if err != nil {
		return nil, err
	}
	query, err := db.Prepare(`SELECT company FROM oui WHERE id=?`)
	if err != nil {
		return nil, err
	}
	return &sqliteDB{db: db, query: query}, nil
}

func (db *sqliteDB) Close() error {
	return db.db.Close()
}

func (db *sqliteDB) Query(oui string) (company string, err error) {
	r := db.query.QueryRow(oui)
	err = r.Scan(&company)
	return
}

func prepare() {
	_, err := os.Stat(OUI_LIST)
	if err != nil {
		if os.IsNotExist(err) {
			os.MkdirAll(filepath.Dir(OUI_LIST), 0777)
			fmt.Println("Downloading OUI list……")
			if err = downloadOUIList(OUI_LIST); err != nil {
				panic(err)
			}
		} else {
			panic(err)
		}
	}

	_, err = os.Stat(HASHIVE_DB)
	if err != nil {
		if os.IsNotExist(err) {
			os.MkdirAll(filepath.Dir(HASHIVE_DB), 0777)
			fmt.Println("generating Hashive DB……")
			err = genHashiveDB(HASHIVE_DB)
			if err != nil {
				panic(err)
			}
		} else {
			panic(err)
		}
	}

	_, err = os.Stat(SQLITE_DB)
	if err != nil {
		if os.IsNotExist(err) {
			os.MkdirAll(filepath.Dir(SQLITE_DB), 0777)
			fmt.Println("generating SQLite DB……")
			err = genSqliteDB(SQLITE_DB)
			if err != nil {
				panic(err)
			}
		} else {
			panic(err)
		}
	}
}

var hashiveBench *hashive.Hashive
var sqliteBench *sqliteDB

func TestMain(m *testing.M) {
	prepare()

	var closeHashive func() error
	var err error
	hashiveBench, closeHashive, err = hashive.Open(HASHIVE_DB, -1)
	if err != nil {
		panic(err)
	}
	defer closeHashive()

	sqliteBench, err = newSqliteDB(SQLITE_DB)
	if err != nil {
		panic(err)
	}
	defer sqliteBench.Close()
	m.Run()
}

func TestOUIHashive(t *testing.T) {
	h, closeDB, err := hashive.Open(HASHIVE_DB, -1)
	if err != nil {
		t.Fatal(err)
	}
	defer closeDB()

	if company, err := h.Query("AC319D"); err != nil {
		t.Fatal(err)
	} else if company != "Shenzhen TG-NET Botone Technology Co.,Ltd." {
		t.Fatal(company)
	}

	if company, err := h.Query("ABCDEF"); err != hashive.ErrNotFound {
		t.Fatal(err)
	} else if company != nil {
		t.Fatal(company)
	}

	if company, err := h.Query("004023"); err != nil {
		t.Fatal(err)
	} else if company != "LOGIC CORPORATION" {
		t.Fatal(company)
	}
}

func TestOUISqlite(t *testing.T) {
	db, err := newSqliteDB(SQLITE_DB)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()

	if company, err := db.Query("AC319D"); err != nil {
		t.Fatal(err)
	} else if company != "Shenzhen TG-NET Botone Technology Co.,Ltd." {
		t.Fatal(company)
	}

	if company, err := db.Query("ABCDEF"); err != sql.ErrNoRows {
		t.Fatal(err)
	} else if company != "" {
		t.Fatal(company)
	}

	if company, err := db.Query("004023"); err != nil {
		t.Fatal(err)
	} else if company != "LOGIC CORPORATION" {
		t.Fatal(company)
	}
}

var benchmarkArgs = []string{"AC319D", "004023", "248602"}

func Benchmark_OUI_Hashive(b *testing.B) {
	for b.Loop() {
		for _, arg := range benchmarkArgs {
			hashiveBench.Query(arg)
		}
	}
}

func Benchmark_OUI_SQLite(b *testing.B) {
	for b.Loop() {
		for _, arg := range benchmarkArgs {
			sqliteBench.Query(arg)
		}
	}
}
