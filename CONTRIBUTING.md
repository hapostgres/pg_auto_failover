## Contributing

This project welcomes contributions and suggestions. Most contributions
require you to agree to a Contributor License Agreement (CLA) declaring that
you have the right to, and actually do, grant us the rights to use your
contribution. For details, visit https://cla.microsoft.com.

When you submit a pull request, a CLA-bot will automatically determine
whether you need to provide a CLA and decorate the PR appropriately (e.g.,
label, comment). Simply follow the instructions provided by the bot. You
will only need to do this once across all repositories using our CLA.

This project has adopted the [Microsoft Open Source Code of
Conduct](https://opensource.microsoft.com/codeofconduct/). For more
information see the [Code of Conduct
FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or contact
[opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional
questions or comments.

### Following our coding conventions

We format all our code using the coding conventions in the
[citus_indent](https://github.com/citusdata/tools/tree/develop/uncrustify)
tool. This tool uses uncrustify under the hood.

```bash
# Uncrustify changes the way it formats code every release a bit. To make sure
# everyone formats consistently we use version 0.68.1:
curl -L https://github.com/uncrustify/uncrustify/archive/uncrustify-0.68.1.tar.gz | tar xz
cd uncrustify-uncrustify-0.68.1/
mkdir build
cd build
cmake ..
make -j5
sudo make install
cd ../..

git clone https://github.com/citusdata/tools.git
cd tools
make uncrustify/.install
```

After installing like this you can run the following before committing:
```bash
citus_indent
```

You can also run the following to automatically format all the files that you
have changed before committing.

```bash
cat > .git/hooks/pre-commit << __EOF__
#!/bin/bash
citus_indent --check --diff || { citus_indent --diff; exit 1; }
__EOF__
chmod +x .git/hooks/pre-commit
```

### Producing the documentation diagrams

The diagrams are TikZ sources, which means they're edited with your usual
editor tooling. The diagrams are actually code, and the compilation tool
chain involve the following software:

  - LuaTex
  - TikZ
  - pdftocairo, found in the poppler software

Current TeX distributions should include luatex and tikz for you already.
One such distribution is TexLive and is widely available.
