# ucp
ucp offers a set of commands to copy files and directories from one place to another.
ucp means ultra(or usb) copy.
Becasue I initially wrote this program to copy files to udisks.
In linux, writing cache makes cp fast but you must WAIT A LONG LONG TIME on enjecting the udisk because system will flush the data to udisk(command : sync).
Unlike Windows, you just wait on copying process.

Besides that, sync is very slow on udisks, thus the total time of copying is longer In Linux than Windows.


## Usage
```
Usage: ucp [options] <source> [<source1> ...] <destination>
Copy file from <sources> to <destination>
Options:
        -h, --help                              Print this help
        -q, --quiet                             Print nothing
        -f, --force                             Overwrite exists files
        -i, --ignore                            Ignore(Skip) exists files
        -d, --directio                          Disable direct I/O(DMA). Default enable
        -s, --sync                              Disable sync data after copy(flush). Default enable 
        -v, --verbose                           Print more information
For more information, please visit https://github.com/Delta-in-hub/ucp
```

## Simple Benchmark

```bash
> pwd
/run/media/delta/A875-5E31
> du -s ~/zerofile
97660   /home/delta/zerofile
> sync
> time cp ~/zerofile ./ && time sync
cp -i ~/zerofile ./  0.00s user 0.09s system 43% cpu 0.199 total
sync  0.00s user 0.04s system 0% cpu 34.161 total
> rm zerofile
> ucp ~/zerofile ./
[========================================] 1/1 , 100.00% , 9.77 MB/s
9.77 s, copyed 1 files, 100.00 MB to ./
```
34.2s vs 9.77s , a huge speedup , at lease on my udisk.