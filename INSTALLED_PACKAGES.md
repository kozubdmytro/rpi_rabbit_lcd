# Packages installed for native DRM output

Initial prototype: no packages installed. Native DRM optimization installed
`libdrm-dev` using `apt-get install -y --no-install-recommends libdrm-dev`.
APT installed these ten packages (759 kB download, about 2.65 MB installed):

| Package | Version |
| --- | --- |
| libdrm-dev | 2.4.134-3~bpo13+1+rpt1 |
| libdrm-amdgpu1 | 2.4.134-3~bpo13+1+rpt1 |
| libdrm-etnaviv1 | 2.4.134-3~bpo13+1+rpt1 |
| libdrm-freedreno1 | 2.4.134-3~bpo13+1+rpt1 |
| libdrm-intel1 | 2.4.134-3~bpo13+1+rpt1 |
| libdrm-nouveau2 | 2.4.134-3~bpo13+1+rpt1 |
| libdrm-radeon1 | 2.4.134-3~bpo13+1+rpt1 |
| libdrm-tegra0 | 2.4.134-3~bpo13+1+rpt1 |
| libpciaccess-dev | 0.17-3+b3 |
| libpciaccess0 | 0.17-3+b3 |

The additional driver libraries are dependencies of the distribution's
`libdrm-dev` package. The application links to the existing `libdrm.so.2`, not
these other GPU libraries. `pkg-config` was already installed. No desktop,
compositor, display server, or browser was installed.
