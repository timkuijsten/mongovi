# mongovi

mongovi is a command line interface for MongoDB. It can be used interactively
and non-interactive and is made to be ergonomic for ad hoc querying and
manipulation of documents in a database.

Features:
* easy integration into shell pipelines by reading and writing
  [MongoDB Extended JSON] via stdin/stdout
* move around databases and collections using the common `cd` idiom
* easy and secure MongoDB authentication using ~/.mongovi
* emacs-like and vi-like [key bindings]
* tab-completion of commands, databases and collections


## Usage examples

### Interactive

Open database *raboof* and collection *bar*:

```sh
$ mongovi raboof/bar
/raboof/bar> 
```

Change collection from bar to qux and insert a document:

```
/raboof/bar> cd ../qux
/raboof/qux> insert { _id: "doc1", foo: "bar" }
```

Query for all documents with *foo* equal to *bar*:

```
/raboof/qux> find { foo: "bar" }
{ "_id" : "doc1", "foo" : "bar" }
```

Quick search on \_id:

```
/raboof/qux> find doc1
{ "_id" : "doc1", "foo" : "bar" }
```

All commands can be abbreviated to the shortest non-ambiguous form, so `find`
can be abbreviated to `f` since no other command starts with an *f*.

Use an aggregation query to filter on documents with *foo* equal to *bar*. Note
that the *aggregate* command can be abbreviated to *a*.

```
/raboof/qux> a [{ $project: { foo: true } }, { $match: { foo: "bar" } }]
{ "_id" : "doc1", "foo" : "bar" }
```


### Non-interactive

Let's start with listing all databases:

```sh
$ echo ls | mongovi
db1
db2
```

Insert two documents into collection *collx* of database *db1* via stdin using a
POSIX shell here-document:

```sh
$ mongovi db1/collx <<'eof'
insert { _id: "doc1", price: 25, foo: "bar", x: true }
insert { _id: "doc2", price: 10, foo: "bar", qux: "raboof" }
eof
```

Now query for all documents with *foo* equal to *bar* in the collection *collx*
in database *db1*:

```sh
$ echo 'find { foo: "bar" }' | mongovi db1/collx
{ "_id" : "doc1", "price" : 25, "foo" : "bar", "x" : true }
{ "_id" : "doc2", "price" : 10, "foo" : "bar", "qux" : "raboof" }
```

A more powerful example in which we use [jq(1)] to filter out the price of the
JSON objects output by mongovi. Please note that all commands can be abbreviated
to the shortest non-ambiguous form, so `find` can be abbreviated to `f` since no
other command starts with an *f*.

```sh
$ echo 'f { foo: "bar" }' | mongovi db1/collx | jq -c 'del(.price)'
{"_id":"doc1","foo":"bar","x":true}
{"_id":"doc2","foo":"bar","qux":"raboof"}
```

And at last it should be noted that mongovi can batch insert by reading one JSON
object per line from stdin when the `-i` switch is used. Let's copy all objects
where *foo* is *bar* to another database/collection with the *price* attribute
stripped using [jq(1)]:

```sh
$ echo 'f { foo: "bar" }' | mongovi db1/collx | jq -c 'del(.price)' | mongovi -i db2/colly
inserted 2 documents
```


### vi key bindings

vi key bindings can be enabled with a standard editline command. Just make sure
your [editrc(5)] contains `bind -v`:

```sh
echo 'bind -v' >> ~/.editrc
```


## Config file

If the file `~/.mongovi` exists, the first line is read and expected to be a
valid mongodb [connection string], possibly containing a username and password.


## Installation

### Build requirements

* C compiler (with C17 support)
* make
* [libedit]
* [mongo-c-driver]


### Run-time requirements

* [libedit]
* [mongo-c-driver]


### Pre-compiled .deb package for Debian and Ubuntu

Download [mongovi_2.0.0-1_amd64.deb](https://netsend.nl/mongovi/v2.0.0/mongovi_2.0.0-1_amd64.deb),
verify the checksum and install.

```sh
$ sha256sum mongovi_2.0.0-1_amd64.deb
# only proceed if the following checksum matches
9da949ace6304479c7af15cb1b37b713548556890743b860c8d3b8c16cc096d4  mongovi_2.0.0-1_amd64.deb
$ sudo apt install libmongoc-1.0-0 libedit2
$ sudo dpkg -i mongovi_2.0.0-1_amd64.deb
```

### Compile latest version on Debian/Ubuntu

*(tested on Debian 9, 11 and Ubuntu 22.04)*

First install the build requirements.

```sh
% sudo apt install make gcc libmongoc-dev libedit-dev
```

Then clone, compile and install mongovi:

```sh
$ git clone https://github.com/timkuijsten/mongovi.git
$ cd mongovi
$ make
$ sudo make install
```


### Compile on macOS

*(tested on macOS 11.7)*

Download, verify, compile and install the [mongo-c-driver] version 1.23.2 (which
needs cmake).

```sh
% cd ~
% curl -sLO https://github.com/mongodb/mongo-c-driver/releases/download/1.23.2/mongo-c-driver-1.23.2.tar.gz
% shasum -a 256 mongo-c-driver-1.23.2.tar.gz
123c358827eea07cd76a31c40281bb1c81b6744f6587c96d0cf217be8b1234e3  mongo-c-driver-1.23.2.tar.gz
% tar zxf mongo-c-driver-1.23.2.tar.gz
% cd mongo-c-driver-1.23.2
% mkdir cmake-build
% cd cmake-build
% cmake -DENABLE_AUTOMATIC_INIT_AND_CLEANUP=OFF ..
% make
% sudo make install
```

Then clone, compile and install mongovi:

```sh
% cd ~
% git clone https://github.com/timkuijsten/mongovi.git
% cd mongovi
% make
% sudo make install
```


## Tests

```sh
$ make test
```


## Documentation

For documentation please refer to the [manpage].


## History

This project is a continuation of [node-mongovi]. *node-mongovi* always had a
limited set of key bindings and suffered from a lack of maintenance which became
more apparent with every new node and mongodb release. Furthermore, nodejs
shouldn't be a requirement for running a mongo cli.


## License

ISC

Copyright (c) 2016, 2022 Tim Kuijsten

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

---

jsmn.h and jsmn.c are part of [JSMN] which is distributed under the MIT
license.


[jq(1)]: https://stedolan.github.io/jq/
[MongoDB Extended JSON]: https://docs.mongodb.com/manual/reference/mongodb-extended-json/
[libedit]: http://cvsweb.netbsd.org/bsdweb.cgi/src/lib/libedit/?sortby=date#dirlist
[mongo-c-driver]: https://mongoc.org/
[Homebrew]: https://brew.sh/
[manpage]: https://netsend.nl/mongovi/mongovi.1.html
[JSMN]: https://zserge.com/jsmn/
[editrc(5)]: https://man.openbsd.org/editrc.5
[editline(7)]: https://man.openbsd.org/editline.7
[editline(3)]: https://man.openbsd.org/editline.3
[key bindings]: https://man.openbsd.org/editline.7#Input_character_bindings
[connection string]: https://docs.mongodb.com/manual/reference/connection-string/
[node-mongovi]: https://www.npmjs.com/package/mongovi
