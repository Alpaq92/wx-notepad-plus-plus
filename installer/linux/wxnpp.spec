# RPM spec for wxNotepad++. Deliberately has no %prep/%build sections - there is no source tarball
# to unpack or compile here, only the already-built wxnpp binary (installer/linux/build-rpm.sh passes
# _srcdir pointing at the repo root, already built via `cmake --build build --target wxnpp`). This is
# the same well-established "binary redistribution" spec pattern used for vendor-supplied prebuilt
# software; %install just stages the existing build/bin/ output into %{buildroot}.
#
# Same /opt/wxnpp + /usr/bin symlink layout as build-deb.sh, for the same reason: the app resolves
# every resource path relative to its own executable, so keeping the binary and its resources
# co-located (rather than the traditional FHS split of exe in /usr/bin, resources in /usr/share)
# needs zero runtime code changes to work - this project has no Linux machine to verify a
# resource-path code change against, only CI.

Name:           wxnpp
Version:        %{_version}
Release:        1%{?dist}
Summary:        Experimental cross-platform Notepad++-faithful editor
License:        GPLv3+
URL:            https://github.com/Alpaq92/wx-notepad-plus-plus
BuildArch:      x86_64
Requires:       gtk3

%description
wxWidgets-based reimplementation of Notepad++, reusing Scintilla and Lexilla,
with an original permissive plugin API and optional Notepad++ ABI plugin
compatibility.

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/opt/wxnpp
cp -r %{_srcdir}/build/bin/. %{buildroot}/opt/wxnpp/
rm -rf %{buildroot}/opt/wxnpp/nib/nib_test_plugin.so %{buildroot}/opt/wxnpp/plugins
mkdir -p %{buildroot}/usr/bin
ln -s /opt/wxnpp/wxnpp %{buildroot}/usr/bin/wxnpp
mkdir -p %{buildroot}/usr/share/applications
install -m 644 %{_srcdir}/installer/linux/wxnpp.desktop %{buildroot}/usr/share/applications/wxnpp.desktop
mkdir -p %{buildroot}/usr/share/icons/hicolor/scalable/apps
install -m 644 "%{_srcdir}/resources/wxNotepad++.svg" %{buildroot}/usr/share/icons/hicolor/scalable/apps/wxnpp.svg

%files
/opt/wxnpp
/usr/bin/wxnpp
/usr/share/applications/wxnpp.desktop
/usr/share/icons/hicolor/scalable/apps/wxnpp.svg

%changelog
* Sun Jul 05 2026 wxNotepad++ Project <noreply@wx-notepad-plus-plus.invalid> - 0.1.0-1
- Initial packaged release
