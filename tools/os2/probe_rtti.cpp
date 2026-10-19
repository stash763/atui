/* Which switch enables RTTI, and does the one already in defs.rsp do anything?

   Compiling tvision's tobjstrm.cpp failed with E875 "RTTI must be enabled to use
   feature (use 'xr' option)" at the dynamic_cast<void *> in ipstream::readData.
   The message names 'xr', but `wpp386 -?` on this installation prints seven lines
   and no option list, so the flags in defs.rsp are checked by behaviour: -xr
   against the very feature that asked for it, and -xs -- already in the flags, from
   before anything linked, and never verified -- against what it is supposed to do
   (turn stack checking off).

   Build each variant and look at which pass:

     wpp386 -bt=os2 -5 -od -w3      -fo=a0.obj probe_rtti.cpp   expect E875
     wpp386 -bt=os2 -5 -od -w3 -xr  -fo=a1.obj probe_rtti.cpp   expect clean
     wpp386 -bt=os2 -5 -od -w3 -vr  -fo=a2.obj probe_rtti.cpp   unknown
*/

struct Base
{
    virtual ~Base() {}
};

struct Derived : Base
{
    int value;
};

#include <stdio.h>

int main( void )
{
    Derived   derived;
    Base     *base = &derived;
    void     *address = dynamic_cast<void *>( base );

    printf( "dynamic_cast<void *> -> %s\n", address ? "ok" : "null" );
    printf( "RTTI-OK\n" );
    return address == 0;
}
