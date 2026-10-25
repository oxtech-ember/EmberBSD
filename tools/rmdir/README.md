# Native rmdir bootstrap

Origin: EmberBSD (AI-assisted). The canonical C source retains its license.

The ordinary [host recipe](Makefile) uses the complete [OS source](../../bin/rmdir/rmdir.c)
and normal host compatibility. It does not disable libnbcompat.
The finite [contract](bootstrap.json) uses native GCC16 and the selected host ABI.
It includes the shared [annotation header](../compat/bootstrap_cdefs.h).
Each filesystem case has isolated typed fixtures and exact final-state assertions.
The SDK controls umask and checks paths, modes and link identities without following links.
The contract contains 9 semantic cases.
Source tests do not prove native API behavior. Compiler, runtime, memory and
ordinary cross-host checks remain separate. No external file utility is selected.
