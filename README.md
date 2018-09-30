# Why we need another trusted GRUB 2?

- [Trusted GRUB 2](https://github.com/Rohde-Schwarz-Cybersecurity/TrustedGRUB2)</br>
  No EFI support.

- [CoreOS GRUB 2 fork](https://github.com/coreos/grub)</br>
  See https://github.com/coreos/grub/pull/55.

- Official GRUB 2</br>
  The [discussion](https://lists.gnu.org/archive/html/grub-devel/2017-06/msg00022.html) ends up without further inputs.

So the currect situation drives me creating a new fork based on CoreOS
GRUB 2 fork with some modifications, and then get back to verifier
framework.
