# The MPIR Shim project

The [MPIR Process Acquisition Interface](https://www.mpi-forum.org/docs/) is an [MPI Forum](https://www.mpi-forum.org/) defined interface for debuggers to interact with MPI programs. The [PMIx Standard](https://pmix.github.io/) contains an alternative and more extensible tool interface.

Some PMIx-enabled launchers do not support the MPIR interface, which can be problematic for tools that have not moved from MPIR to PMIx. This project is targeted at those tools. The MPIR Shim project provides most of the MPIR interface to those tools that require it, and behind the scenes uses the PMIx tool interface.

The MPIR Shim does not provide debugging capabilities by itself. It merely provides the symbols and back end functionality to support tools that may choose to use those systems.


## Building the MPIR Shim

What you will need:
 * [OpenPMIx](https://openpmix.github.io/) installation (though any PMIx standard compliant implementation can be used)

```
./configure --prefix=/path-to-install/mpir-shim --with-pmix=/path-to-openpmix-install
make
make install
```

This will create two binaries `mpir` and `mpirc` either of which can be used to wrap the native launcher. The difference is that the former (`mpir`) is written in C++ and the latter (`mpirc`) is written in C. They should be functionally the same.

Additionally, a library is created (`libmpirshim` - both static and shared versions) that can be linked into a launcher library that wants to hide the use the shim from the user.


## Running the MPIR Shim

The MPIR Shim works in a few different modes, namely:
 * Proxy mode
 * Non-proxy mode
 * Attach mode


### Running in proxy mode

**Proxy Mode** : Running the MPIR Shim in a runtime environment where there are no persistent daemons.
For example, `prterun` or `mpirun` used to launch a single job. Those launchers are responsible for launching the daemons, then the application, then cleaning it all up when the job is finished.

```
mpir mpirun -np 2 ./a.out
```

Example of MPIR Shim being used with a legacy version of TotalView that does not support PMIx:

```
totalview -args mpir mpirun -n 32 mpi-program
```

### Running in non-proxy mode

**Non-Proxy Mode** : Running the MPIR Shim in a runtime environment with a persistent daemon.
For example, if you started the PRRTE `prte` process to setup a persistent Distributed Virtual Machine (DVM) environment then use `prun` to launch jobs against the DVM environment.

```
prte --daemonize
mpir -n prun -np 2 ./a.out
pterm
```

### Running in attach mode

**Attach Mode** : Running the MPIR Shim as a front end to attach to a running job by referencing the PMIx server by its PID.

Launch your application (possibly without the MPIR shim)
```
mpirun -np 2 ./a.out
```

Later, attach to the running job by using the PID of `mpirun` (using `1234` for illustration below):
```
mpirc -c 1234
# Or directly with a debugger
gdb mpirc -c 1234
```

The MPIR Shim will extract the `MPIR_proctable` and idle until the application terminates. You can then use a parallel debugger to connect to the `mpirc` process to read the process table and attach to the remote processes.

Note that Attach Mode assumes a Proxy Mode launch at this time. It may not work with the Non-Proxy mode.

## Support

If you have questions or need help either post a GitHub issue (if it is a problem) or email the [OpenPMIx mailing list](https://groups.google.com/forum/?utm_medium=email&utm_source=footer#!forum/pmix).
