# MFT Fast Transcoder
## _Fastest MFT parser, maybe_

[![Build Status](https://travis-ci.org/joemccann/dillinger.svg?branch=master)](https://travis-ci.org/joemccann/dillinger)

MFT Fast Transcoder is a fast forensic tool to analyze MFT of NTFS partitions.
I am focused on Windows, the code makes use of libraries and specific functions of this operating system. 
Also, I am only developing for x64, this is a forensic tool, it is memory intensive and we are in 2021.
This tool extracts in an instant a list of files and folders hosted on an NTFS in CSV format with the following properties:
- File or Folder
- Name with path
- STDINFO dates (modified, acces, metadata and creation) - that are relative to the creation of the record in the MFT
- FILE dates (modified, acces, metadata and creation) - that are relative to the creation of the file in the MFT

(Want to know more? https://www.sans.org/posters/windows-forensic-analysis/)


## Features

- Dump MFT as a file from a mounted partition.
- Analize previously dumped MFT
- Analize the MFT of a mounted partition.
- (TODO) All of the above from an offline partition/disk or ISO/DD file.
- (TODO) Extract another relevant information.


## Usage
For now, there are 3 ways of working:

To dump the MFT from a partition to a file.

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; dump [drive_letter] [MFT_result_path]
```sh
dump c "D:\MFT.file"
```

To parse previously dumped MFT file.

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; transcode [MFT_file_path] [CSV_result_path]
```sh
transcode "D:\MFT.file" "D:\MFT.csv"
```

To direct transcode the MFT of a partition to a CSV file.

&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp; DT [drive_letter] [CSV_result_path]
```sh
DT c "D:\MFT.csv"
```

## TODO

- Work with an offline partition/disk or ISO/DD file.
- Extract another relevant information.
- Improve user experience

## Development

Want to contribute? Great!

The code is intended to be easy to understand and fast to execute.
That's why there isn't a lot of functions and there isn't a efficient code.
Memory is sacrificed in favor of high execution speed.

Likewise, any contribution in the form of code is welcome.

Ideas are worth as gold, so another great way to contribute is by sending your feedback on improvement.

Want to buy me a coffe?
ETH: 0x911C6cC26a9797401FDa69Aeb7d3f69c49A70dC3

## Acknowledgments

Thanks to these other projects I have been able to write this code much faster:
- https://handmade.network/wiki/7002-tutorial_parsing_the_mft --> MFT dump
- https://github.com/dkovar/analyzeMFT --> NTFS fixup
- https://flatcap.org/linux-ntfs/ntfs/index.html --> Basic NTFS knowledge
- https://dillinger.io/ --> To make this README

## License

You can find all the information in the LICENSE file, but basically, it's free ^^

**Free Software, Hell Yeah!**