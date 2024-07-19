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
