# Usage
```bash
# build the audit module
nix build .

# point LD_AUDIT to the module
export LD_AUDIT=$PWD/result/lib/ld-audit-prefer-runpath.so

# (optional) search order of libraries specified here will not be modified
export LAPR_IGNORE_LIBS=libGL.so

# run your program
your-program
```

# Use with Home Manager
```nix
{ inputs, pkgs }: {
  home.sessionVariables.LD_AUDIT = "${inputs.ld-audit-prefer-runpath.packages.${system}.default}/lib/ld-audit-prefer-runpath.so";
}
```

# Known Issues

- This module does not verify content of files found in LD_LIBRARY_PATH. An invalid or incompatible (e.g. 32-bit libraries on a 64-bit system) file may shadow valid libraries and cause errors.
