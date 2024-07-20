# Usage
```bash
# build the audit module
nix build .

# point LD_AUDIT to the module
export LD_AUDIT=$PWD/result/lib/ld-audit-prefer-runpath.so

# (optional) set this to enable the module for non-Nix programs as well
export LAPR_ENABLE_FOR_ALL=1

# (optional) search order of libraries specified here will NOT be modified
export LAPR_IGNORE_LIBS=libGL.so

# (optional) libraries specified here will NOT be searched in directories specified by LD_LIBRARY_PATH
export LAPR_DISABLE_LIBPATH_LIBS=libc.so.6

# run your program
your-program
```

# Use with Home Manager
```nix
{ inputs, pkgs }: {
  nixpkgs.overlays = [ inputs.ld-audit-prefer-runpath.overlays.default ];
  home.sessionVariables.LD_AUDIT = "${pkgs.ld-audit-prefer-runpath}/lib/ld-audit-prefer-runpath.so";
}
```
