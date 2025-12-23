from building import *
import os

cwd = GetCurrentDir()

inc = [cwd + '/include']

src = Glob('src/*.c')
src += Glob('src/api/*.c')
src += Glob('src/libfs/*.c')
src += Glob('src/namei/*.c')
src += Glob('src/uio/*.c')
src += Glob('src/dentry/*.c')
src += Glob('src/file/*.c')
src += Glob('src/inode/*.c')
src += Glob('src/mnt/*.c')

group = DefineGroup('vfs', src, depend=['CONFIG_VFS'], CPPPATH=inc)

Return('group')
