import sys
import os

# tablespace tests import as "from tests.tablespaces.xxx import ..." which
# requires the project root (parent of tests/) on sys.path.  pytest does not
# add it automatically; nosetests used to, by treating tests/__init__.py as a
# package root.  Add it here so both test runners work.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
