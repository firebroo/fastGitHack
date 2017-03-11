package main

import (
    "fmt"
    "os"
    "bytes"
    "net/http"
    "io/ioutil"
    "compress/zlib"
    "encoding/binary"
)

var url string = "http://localhost/.git"

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

func TaskFunc(cb ceBody) {
    fileName := cb.Name
    fileSize := cb.EntryBody.Size

    prefix, suffix :=  Bytes2Sha1(cb.EntryBody.Sha1)
    url := fmt.Sprintf("%s/objects/%s/%s", url, prefix, suffix)
    ret := HttpGet(url)
    blobHead := fmt.Sprintf("blob %d", fileSize)
    blobHeadLen := len(blobHead) + 1
    ioutil.WriteFile(fileName, UnzipBytes(ret)[blobHeadLen:], 0666)
}

func ParseIndex(index []byte) {
    var h head
    var thisEntryBody entryBody
    var thisEntrySize int
    binary.Read(bytes.NewBuffer(index), binary.BigEndian, &h)

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
                TaskFunc(entry)
            } else { /* good luck */}
        }
    } else {
      os.Exit(-1)
    }
}

func main() {
    index := HttpGet(url + "/index")
    ParseIndex(index)
}
