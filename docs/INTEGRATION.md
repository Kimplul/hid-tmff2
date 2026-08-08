Some distros require more complicated installation methods than what's outlined
in the README. This page is intended to collect information about the topic.

# NixOS

The previous discussion was in https://github.com/Kimplul/hid-tmff2/issues/71,
but it seems to have been removed by GitHub. For now, please refer to

https://search.nixos.org/packages?query=hid-tmff2

# Bazzite, other immutable distros

In the general case, installing external modules yourself is not possible. Some
exceptions apply, SteamOS allows temporarily making the root drive writeable
with `sudo steamos-readonly disable`, but the extra kernel modules will have to
be manually reinstalled after every system update.

Check your distributions packages if `hid-tmff2` exists. If not, please try to
get it packaged (or, maybe preferably, please pester me into getting this
module upstreamed :p).
