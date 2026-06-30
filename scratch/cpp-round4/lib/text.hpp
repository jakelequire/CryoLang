#pragma once
// const char* in and out (C string handling).
unsigned cstr_len(const char* s);
const char* pick_word(int i);     // returns a static const char*
// static data member + nested class probes.
struct Config {
    static const int MAX_ITEMS;   // static data member
    int n;
    int doubled_n() const;
    struct Entry { int key; int val; };   // nested class
};
int entry_sum(const Config::Entry& e);
