# Usage
```bash
# build the audit module
nix build .

# point LD_AUDIT to the module
export LD_AUDIT=$PWD/result/lib/ld-audit-search-mod.so

# set config file path
# see examples/config.yaml for documentation
export LD_AUDIT_SEARCH_MOD_CONFIG=/path/to/config.yaml

# run your program
your-program
```

# Use with NixOS
```nix
# flake.nix
{
  inputs.lasm.url = "github:DDoSolitary/ld-audit-search-mod";
  outputs = { nixpkgs, lasm }: {
    nixosConfigurations.your-machine = nixpkgs.lib.nixosSystem {
      modules = [
        ({ pkgs, ... }: {
          nixpkgs.overlays = [ lasm.overlays.default ];
          environment.sessionVariables = {
            LD_AUDIT = "${pkgs.ld-audit-search-mod}/lib/libld-audit-search-mod.so";
            LD_AUDIT_SEARCH_MOD_CONFIG = pkgs.writeText "lasm-config.yaml" (builtins.toJSON {
              rules = [
                {
                  cond.rtld = "nix";
                  libpath.save = true;
                  default.prepend = [
                    { saved = "libpath"; }
                    { dir = "${pkgs.stdenv.cc.cc.lib}/lib"; }
                  ];
                }
              ];
            });
          };
        })
      ];
    };
  };
}
```
