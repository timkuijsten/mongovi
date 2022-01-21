# mongovi

mongovi is a command line interface for MongoDB.

Features:
* Emacs-like and vi-like [key bindings] via [libedit]
* Tab-completion of commands, databases and collections
* move around databases and collections using the common `cd` idiom
* integrates into shell pipelines by reading and writing [MongoDB Extended JSON]
  on stdin/stdout
* read a username and password from a simple config file

Status: **stable**

mongovi is primarily developed on macOS and tested on Debian and Ubuntu.


## Installation

### Debian and Ubuntu

Using the binary package is the easiest way to get started on a Debian based
system. On either Debian Stretch or Ubuntu 16.04 the following should be issued:

```sh

$ wget https://netsend.nl/mongovi/mongovi_1.0.0~rc5-2_amd64.deb
$ sha256sum mongovi_1.0.0~rc5-2_amd64.deb    # only proceed if this checksum matches
9a1639684d3337b72baaf8f2ce96caa36faeb150e2dfb6a5cc74be7452d7ccdf  mongovi_1.0.0~rc5-2_amd64.deb
$ sudo apt-get install -y libmongoc-1.0-0
$ sudo dpkg -i mongovi_1.0.0~rc5-2_amd64.deb
```


### macOS

*(tested on macOS 10.15)*

Download, verify and compile the [mongo-c-driver] version 1.14.1 (which needs
cmake).

```sh
% cd ~
% curl -sLO https://github.com/mongodb/mongo-c-driver/releases/download/1.14.1/mongo-c-driver-1.14.1.tar.gz
% shasum -a 256 mongo-c-driver-1.14.1.tar.gz
84fca347a6818e5ed6db50e06eb6d33538346b49bf0ba7475d7dbcecacac2ec6  mongo-c-driver-1.14.1.tar.gz
% tar zxf mongo-c-driver-1.14.1.tar.gz
% export _mongoc=~/mongo-c-driver-1.14.1
% cd "$_mongoc"/build
% cmake -DENABLE_SSL=OFF -DENABLE_SASL=OFF -DENABLE_ZLIB=OFF ..
% make
```

Then clone, compile and install mongovi:

```sh
% cd ~
% git clone https://github.com/timkuijsten/mongovi.git
% cd mongovi
% cc \
  -I "$_mongoc"/src/libmongoc/src \
  -I "$_mongoc"/src/libmongoc/src/mongoc \
  -I "$_mongoc"/src/libbson/src \
  -I "$_mongoc"/src/libbson/src/bson \
  -I "$_mongoc"/build/src/libbson/src \
  -I "$_mongoc"/build/src/libmongoc/src \
  -o mongovi mongovi.c jsonify.c main.c  prefix_match.c shorten.c jsmn.c \
  compat/reallocarray.c \
  "$_mongoc"/build/src/libmongoc/libmongoc-static-1.0.a \
  "$_mongoc"/build/src/libbson/libbson-static-1.0.a \
  -ledit -lresolv
% sudo make install
```


### Build requirements

* C compiler (with reasonable C99 support)
* BSD or GNU make
* [libedit]
* [mongo-c-driver] (which needs cmake)


### Run-time requirements

* [libedit]
* [mongo-c-driver]


## Documentation

For documentation please refer to the [manpage].


## Usage examples

### Interactive

Open database *raboof* and collection *bar*:

```sh
$ mongovi /raboof/bar
/raboof/bar> 
```

Change collection from bar to qux:

```
/raboof/bar> cd ../qux
/raboof/qux> 
```

List all documents where *foo* is *bar*, using `find` which can be abbreviated
to `f`:

```
/raboof/qux> f { foo: "bar" }
{ "foo" : "bar" }
```

Quick search on object id:

```
/raboof/qux> f 57c6fb00495b576b10996f64
{ "_id" : { "$oid" : "57c6fb00495b576b10996f64" }, "foo" : "bar" }
```

Use an aggregation query to filter on documents where *foo* is *bar*. Note that
*aggregate* can be abbreviated to *a*.

```
/raboof/qux> a [{ $project: { foo: true } }, { $match: { foo: "bar" } }]
{ "foo" : "bar" }
```

### Non-interactive

List all databases using `f`, an abbreviation of `find`:

```sh
$ echo f | mongovi
raboof
```

Copy some documents from */raboof/qux* to */raboof/baz*:

```sh
$ echo 'f { foo: "bar" }' | mongovi /raboof/qux | mongovi -i /raboof/baz
```

### vi key bindings

vi key bindings can be enabled with a standard editline command. Just make sure
your [editrc(5)] contains `bind -v`:

```sh
echo "bind -v" >> ~/.editrc
```


## Config file

If the file ~/.mongovi exists, the first line is read and expected to be a valid
mongodb [connection string], possibly containing a username and password.


## Tests

```sh
$ make test
```


## Wanted

* UTF-8 support


## History

This project is a continuation of [node-mongovi]. *node-mongovi* always had a
limited set of key bindings and suffered from a lack of maintenance which became
more apparent with every new node and mongodb release. Furthermore, nodejs
shouldn't be a requirement for running a mongo cli.


## License

ISC

Copyright (c) 2016 Tim Kuijsten

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

mongovi uses the JSMN [JSON parser] which is distributed under the MIT license.


[MongoDB Extended JSON]: https://docs.mongodb.com/manual/reference/mongodb-extended-json/
[libedit]: http://cvsweb.netbsd.org/bsdweb.cgi/src/lib/libedit/?sortby=date#dirlist
[mongo-c-driver]: http://mongoc.org/
[Homebrew]: http://brew.sh/
[manpage]: https://netsend.nl/mongovi/mongovi.1.html
[JSON parser]: http://zserge.com/jsmn.html
[editrc(5)]: http://man.openbsd.org/editrc.5
[editline(7)]: http://man.openbsd.org/editline.7
[editline(3)]: http://man.openbsd.org/editline.3
[key bindings]: http://man.openbsd.org/editline.7#Input_character_bindings
[connection string]: https://docs.mongodb.com/manual/reference/connection-string/
[node-mongovi]: https://www.npmjs.com/package/mongovi
