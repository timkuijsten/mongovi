# mongovi

mongovi is a cli for MongoDB. Both emacs and vi [key bindings] are supported via
libedit.

Status: **release-candidate**, make sure you have backups of your database


mongovi is primarily developed and tested on OS X 10.11, Debian and Ubuntu.


## Installation

### Debian and Ubuntu

Using the binary package is the easiest way to get started on a Debian based system:

    $ wget https://netsend.nl/mongovi/mongovi_1.0.0-1_amd64.deb
    $ sha256sum mongovi_1.0.0-1_amd64.deb    # only proceed if this checksum matches
    9a33f86d5f74b77085d0f0801c4a48d348bc2eeb952299c7b1b828b1f66d4db7  mongovi_1.0.0-1_amd64.deb
    $ dpkg -i mongovi_1.0.0-1_amd64.deb

### macOS

First compile and install libmongoc and libbson:

    $ curl -LO https://github.com/mongodb/mongo-c-driver/releases/download/1.4.0/mongo-c-driver-1.4.0.tar.gz
    $ sha256sum mongo-c-driver-1.4.0.tar.gz    # only proceed if this checksum matches
    2bc6ea7fd8db15250910a7c72da7d959e416000bec2205be86b52d2899f6951b  mongo-c-driver-1.4.0.tar.gz
    $ tar zxf mongo-c-driver-1.4.0.tar.gz
    $ cd mongo-c-driver-1.4.0/
    $ ./configure --enable-man-pages=yes
    $ make
    $ sudo make install
    $ ldconfig
    $ cd

Then compile and install mongovi:

    $ git clone https://github.com/timkuijsten/mongovi.git
    $ cd mongovi
    $ make
    $ sudo make install


## Documentation

For documentation please refer to the [manpage].


## Usage examples

### Interactive

Open database *raboof* and collection *bar*:

    $ mongovi /raboof/bar
    /raboof/bar> 

Change collection from bar to qux:

    /raboof/bar> cd ../qux
    /raboof/qux> 

List all documents where *foo* is *bar*:

    /raboof/qux> find { foo: "bar" }
    { "foo" : "bar" }

Quick search on object id:

    /raboof/qux> find 57c6fb00495b576b10996f64
    { "_id" : { "$oid" : "57c6fb00495b576b10996f64" }, "foo" : "bar" }

Use an aggregation query to filter on documents where *foo* is *bar*. Note that
*aggregate* is abbreviated to *a*.

    /raboof/qux> a [{ $project: { foo: true } }, { $match: { foo: "bar" } }]
    { "foo" : "bar" }

### Non-interactive

List all databases:

    $ echo ls | mongovi
    raboof

Copy some documents from */raboof/qux* to */raboof/baz*:

    $ echo 'find { foo: "bar" }' | mongovi /raboof/qux | mongovi -i /raboof/baz

### vi key bindings

vi key bindings can be enabled with a standard editline command. Just make sure
your [editrc(5)] contains `bind -v`:

```sh
echo "bind -v" >> ~/.editrc
```


## ~/.mongovi

If this file exists, the first line is read and expected to be a valid mongodb
[connection string], possibly containing a username and password.


## Known issues

Currently no support for UTF-8.


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


[manpage]: https://netsend.nl/mongovi/mongovi.1.html
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
