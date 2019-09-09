# Vendored-in librairies

## log.c

A very simple lib for handling logs in C is available at

  https://github.com/rxi/log.c

It says that

  log.c and log.h should be dropped into an existing project and compiled
  along with it.

So this directory contains a _vendored-in_ copy of the log.c repository.

## SubCommands.c

The single-header library is used to implement parsing "modern" command lines.

## Configuration file parsing

We utilize the "ini.h" ini-file reader from https://github.com/mattiasgustavsson/libs

## JSON

The parson librairy at https://github.com/kgabis/parson is a single C file
and MIT licenced. It allows parsing from and serializing to JSON.

## HTTP and JSON

We use https://github.com/vurtun/mmx implementation of an HTTP server in C,
with some extra facilities such as JSON parsing. The library is licenced
separately for the different files:

  - json.h is public domain
  - lexer.h is zlib licence, required by json.h
  - sched.h is zlib licence
  - vec.h is zlib licence
  - web.h is BSD licence

