{
  description = "我修复了 Linux 非 KDE 桌面环境没有 winver 的 bug";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          winver = pkgs.stdenv.mkDerivation {
            pname = "winver";
            version = "0.1.0";

            src = self;

            nativeBuildInputs = [ pkgs.pkg-config pkgs.glib ];
            buildInputs = [ pkgs.gtk4 ];

            makeFlags = [ "PREFIX=$(out)" ];
            installTargets = [ "install" ];

            meta = {
              description = "winver-style About dialog for Linux distributions, with a demo mode for 30 distros";
              homepage = "https://github.com/DeepslateQAQ/linux-winver";
              license = nixpkgs.lib.licenses.gpl3Only;
              platforms = nixpkgs.lib.platforms.linux;
            };
          };
          default = self.packages.${system}.winver;
        });
    };
}
