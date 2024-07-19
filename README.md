# Usage
```bash
nix build .
LD_AUDIT=$PWD/result/lib/ld-audit-prefer-runpath.so your-program
```

# Use with Home Manager
```nix
{ inputs, pkgs }: {
  home.sessionVariables.LD_AUDIT = "${inputs.ld-audit-prefer-runpath.packages.${system}.default}/lib/ld-audit-prefer-runpath.so";
}
```

# Known Issues

- This module does not verify content of files found in LD_LIBRARY_PATH. An invalid or incompatible (e.g. 32-bit libraries on a 64-bit system) file may shadow valid libraries and cause errors.
- There are valid use cases of allowing LD_LIBRARY_PATH to override libraries from Nix store (e.g. using libGL.so from the system). Currently no exemption mechanism is implemented to support such cases.
