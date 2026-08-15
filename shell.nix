{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  packages = with pkgs; [
    gtk4
    pkg-config
    # fonts so CJK text renders correctly in the dev shell
    noto-fonts
    noto-fonts-cjk-sans
  ];
}
