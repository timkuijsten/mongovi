# mongovi

mongovi is a cli for MongoDB that uses libedit for line editing and [key bindings].

Status: **alpha**, only use it if you're not afraid to dive into the source code.


## Requirements:
* [MongoDB C Driver] 1.4.0
* [editline(3)] ships with OS X

Only tested with OS X 10.11, Ubuntu 12.04 and 14.04.


## Installation

OS X ships with editline, on Ubuntu you have to manually install this:

    $ sudo apt-get install libedit-dev

On OS X 10.11, Ubuntu 12.04 and Ubuntu 14.04, install the mongoc and bson libraries:

    $ curl -LO https://github.com/mongodb/mongo-c-driver/releases/download/1.4.0/mongo-c-driver-1.4.0.tar.gz
    $ sha256sum mongo-c-driver-1.4.0.tar.gz    # only proceed if this checksum matches
    2bc6ea7fd8db15250910a7c72da7d959e416000bec2205be86b52d2899f6951b  mongo-c-driver-1.4.0.tar.gz
    $ tar zxf mongo-c-driver-1.4.0.tar.gz
    $ cd mongo-c-driver-1.4.0/
    $ ./configure --enable-man-pages=yes
    $ make
    $ sudo make install

On Ubuntu 16.04:

    $ sudo apt-get install libmongoc-dev libbson-dev

Now download, compile and install mongovi:

    $ git clone https://github.com/timkuijsten/mongovi.git
    $ cd mongovi
    $ make


## Usage examples

### Interactive

Open database *raboof* and collection *sabar*:

    $ ./mongovi raboof sabar
    /raboof/sabar > 

List all documents:

    /raboof/sabar > find
    { "_id" : { "$oid" : "57c6fb00495b576b10996f64" }, "foo" : "bar" }
    { "_id" : { "$oid" : "57c6fb00495b576b10996f65" }, "foo" : "baz" }
    /raboof/sabar > 

List all documents with *foo* is *bar*:

    /raboof/sabar > find { foo: "bar" }
    { "foo" : "bar" }
    /raboof/sabar > 

Quick search on object id:

    /raboof/sabar > find 57c6fb00495b576b10996f64
    { "_id" : { "$oid" : "57c6fb00495b576b10996f64" }, "foo" : "bar" }
    /raboof/sabar > 

Use an aggregation query to list all documents without the _id field:

    /raboof/sabar > [{ $project: { _id: false, foo: true } }]
    { "foo" : "bar" }
    { "foo" : "baz" }
    /raboof/sabar > 

Same as previous, but filter on documents with *foo* is *bar*:

    /raboof/sabar > [{ $project: { _id: false, foo: true } }, { $match: { foo: "bar" } }]
    { "foo" : "bar" }
    /raboof/sabar > 

### Non-interactive

Show all documents in *raboof.sabar* with *foo* is *bar*:

    $ echo 'find { foo: "bar" }' | ./mongovi raboof sabar
    { "foo" : "bar" }
    $

### vi key bindings

vi key bindings can be enabled with a standard editline command. Just make sure
your [editrc(5)] contains `bind -v`:

```sh
echo "bind -v" >> ~/.editrc
```

## Command-line options

    usage: mongovi database collection


## Commands

* `dbs` list all databases
* `c` list all collections in the current database
* `c coll` change the current collection to `coll`
* `c /db/coll` change the current database to "db" and the collection to "coll"
* `count selector` count the number of documents in the current collection
* `update selector doc` update all documents that match `selector` with `doc`
* `insert doc` insert given document `doc`
* `remove selector` remove all documents that match `selector`
* `find selector` query the collection using `selector`, see [query operators]
* `{...}` alias for find
* `[...]` aggregation pipeline, see [aggregation operators]

If selector is not a JSON document it is treated as a shortcut to search on _id
with type string. Hexadecimal strings of 24 characters are treated as object ids.

See [editline(7)] for a list of supported key bindings.


## ~/.mongovi

If this file exists, the first line is read and expected to be a valid mongodb
[connection string], possibly containing a username and password.


## Wish list

Things that would be nice but are not implemented:
* edit docs with EDITOR
* tab completion


## Tests

    $ make test


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


[JSON parser]: http://zserge.com/jsmn.html
[editrc(5)]: http://man.openbsd.org/editrc.5
[editline(7)]: http://man.openbsd.org/editline.7
[editline(3)]: http://man.openbsd.org/editline.3
[key bindings]: http://man.openbsd.org/editline.7#Input_character_bindings
[MongoDB C Driver]: http://mongoc.org/
[aggregation operators]: https://docs.mongodb.com/manual/reference/operator/aggregation/
[query operators]: https://docs.mongodb.com/manual/reference/operator/query/
[connection string]: https://docs.mongodb.com/manual/reference/connection-string/
[node-mongovi]: https://www.npmjs.com/package/mongovi
