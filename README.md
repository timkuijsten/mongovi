# mongovi

mongovi is a cli for MongoDB that uses libedit for line editing and [key bindings].

Status: **alpha**, only use it if you're not afraid to dive into the source code.


## Requirements:
* [MongoDB C Driver] 1.4.0
* [editline(3)] ships with OS X

Only tested with OS X 10.11, Ubuntu 12.04 and 14.04.

## Installation

Download compile and install the mongo-c driver:

    $ curl -LO https://github.com/mongodb/mongo-c-driver/releases/download/1.4.0/mongo-c-driver-1.4.0.tar.gz
    $ sha256sum mongo-c-driver-1.4.0.tar.gz    # only proceed if this checksum matches
    2bc6ea7fd8db15250910a7c72da7d959e416000bec2205be86b52d2899f6951b  mongo-c-driver-1.4.0.tar.gz
    $ tar zxf mongo-c-driver-1.4.0.tar.gz
    $ cd mongo-c-driver-1.4.0/
    $ ./configure --enable-man-pages=yes
    $ make
    $ sudo make install
    $ cd ~

Download compile and install mongovi:

    $ git clone https://github.com/timkuijsten/mongovi.git
    $ cd mongovi

### OS X 10.11

    $ cc mongovi.c common.c jsmn.c jsonify.c shorten.c -I /usr/local/include/libmongoc-1.0 -I /usr/local/include/libbson-1.0 -lbson-1.0 -lmongoc-1.0 -ledit

### Ubuntu 12.04, 14.04

Make sure libedit is installed before compiling mongovi:

    $ sudo apt-get install libedit-dev
    $ export LD_LIBRARY_PATH="/usr/local/lib/:$LD_LIBRARY_PATH"
    $ cc compat/strlcpy.c compat/strlcat.c mongovi.c common.c jsmn.c jsonify.c shorten.c -I /usr/local/include/libmongoc-1.0 -I /usr/local/include/libbson-1.0 -lmongoc-1.0 -lbson-1.0 -ledit


## Usage examples

Open database *raboof* and collection *sabar*:

    $ ./a.out raboof sabar
    /raboof/sabar > 

Use an empty query filter to list all documents:

    /raboof/sabar > {}
    { "_id" : { "$oid" : "57c6fb00495b576b10996f64" }, "foo" : "bar" }
    { "_id" : { "$oid" : "57c6fb00495b576b10996f65" }, "foo" : "baz" }
    /raboof/sabar > 

Use an aggregation query to list all documents without the _id field:

    /raboof/sabar > [{ $project: { _id: false, foo: true } }]
    { "foo" : "bar" }
    { "foo" : "baz" }
    /raboof/sabar > 

Same as previous, but filter on documents where *foo* is *bar*:

    /raboof/sabar > [{ $project: { _id: false, foo: true } }, { $match: { foo: "bar" } }]
    { "foo" : "bar" }
    /raboof/sabar > 


## Command-line options

    usage: a.out database collection


## Interactive commands

* `dbs` list all databases
* `c` list all collections in the current database
* `c coll` change the current collection to "coll"
* `c /db/coll` change the current database to "db" and the collection to "coll"
* `count` count the number of documents in the current collection
* `update selector update` update using a selector document and update document
* `{...}` query filter, see [query operators]
* `[...]` aggregation pipeline, see [aggregation operators]

See [editline(7)] for a list of supported key bindings.


## ~/.mongovi

If this file exists, the first line is read and expected to be a valid mongodb
[connection string], possibly containing a username and password.


## Tests

    $ cc shorten.c test/shorten.c && ./a.out; echo $?


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


[editline(7)]: http://man.openbsd.org/editline.7
[editline(3)]: http://man.openbsd.org/editline.3
[key bindings]: http://man.openbsd.org/editline.7#Input_character_bindings
[MongoDB C Driver]: http://mongoc.org/
[aggregation operators]: https://docs.mongodb.com/manual/reference/operator/aggregation/
[query operators]: https://docs.mongodb.com/manual/reference/operator/query/
[connection string]: https://docs.mongodb.com/manual/reference/connection-string/
[node-mongovi]: https://www.npmjs.com/package/mongovi
