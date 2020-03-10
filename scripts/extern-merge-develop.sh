#!/bin/bash

# execute from arcana-samples root

git fetch

cd extern

cd arcana-incubator
git merge origin/develop
cd ..

cd clean-core
git merge origin/develop
cd ..

cd nexus
git merge origin/develop
cd ..

cd phantasm-hardware-interface
git merge origin/develop
cd ..

cd phantasm-renderer
git merge origin/develop
cd ..

cd phantasm-viewer
git merge origin/develop
cd ..

cd reflector
git merge origin/develop
cd ..

cd rich-log
git merge origin/develop
cd ..

cd structured-interface
git merge origin/develop
cd ..

cd task-dispatcher
git merge origin/develop
cd ..

## back to root
cd ..