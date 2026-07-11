# RPM spec for wxNote. Deliberately has no %prep/%build sections - there is no source tarball
# to unpack or compile here, only the already-built wxnote binary (installer/linux/build-rpm.sh passes
# _srcdir pointing at the repo root, already built via `cmake --build build --target wxnote`). This is
# the same well-established "binary redistribution" spec pattern used for vendor-supplied prebuilt
# software; %install just stages the existing build/bin/ output into %{buildroot}.
#
# Same /opt/wxnote + /usr/bin symlink layout as build-deb.sh, for the same reason: the app resolves
# every resource path relative to its own executable, so keeping the binary and its resources
# co-located (rather than the traditional FHS split of exe in /usr/bin, resources in /usr/share)
# needs zero runtime code changes to work - this project has no Linux machine to verify a
# resource-path code change against, only CI.

Name:           wxnote
Version:        %{_version}
Release:        1%{?dist}
Summary:        Experimental cross-platform text editor
License:        GPLv3+
URL:            https://github.com/Alpaq92/wx-notepad-plus-plus
BuildArch:      x86_64
Requires:       gtk3

%description
wxWidgets-based cross-platform text editor built on the Scintilla and Lexilla
editing engines, with an original permissive plugin API (Nib) and an optional
Windows compatibility bridge for legacy Notepad++ plugin binaries.

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/opt/wxnote
cp -r %{_srcdir}/build/bin/. %{buildroot}/opt/wxnote/
rm -rf %{buildroot}/opt/wxnote/nib/nib_test_plugin.so %{buildroot}/opt/wxnote/plugins
mkdir -p %{buildroot}/usr/bin
ln -s /opt/wxnote/wxnote %{buildroot}/usr/bin/wxnote
mkdir -p %{buildroot}/usr/share/applications
install -m 644 %{_srcdir}/installer/linux/wxnote.desktop %{buildroot}/usr/share/applications/wxnote.desktop
mkdir -p %{buildroot}/usr/share/icons/hicolor/scalable/apps
install -m 644 "%{_srcdir}/resources/wxnote.svg" %{buildroot}/usr/share/icons/hicolor/scalable/apps/wxnote.svg

%files
/opt/wxnote
/usr/bin/wxnote
/usr/share/applications/wxnote.desktop
/usr/share/icons/hicolor/scalable/apps/wxnote.svg

%changelog
* Sun Jul 05 2026 wxNote Project <noreply@wx-notepad-plus-plus.invalid> - 0.2.0-1
- Edit Popup ContextMenu now opens a real, user-editable context menu
- Linux packaging fixes (flatpak icon/debuginfo/remote-add CI issues)

* Sun Jul 05 2026 wxNote Project <noreply@wx-notepad-plus-plus.invalid> - 0.1.0-1
- Initial packaged release
