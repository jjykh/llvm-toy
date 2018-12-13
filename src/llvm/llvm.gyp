{
    'includes': [
        'llvm.gypi',
    ],
    'targets': [
        {
            'target_name': 'libllvm',
            'type': 'static_library',
            'sources': [ '<@(sources)',],
            'defines': [
                'LLVMLOG_LEVEL=<(llvmlog_level)',
            ],
            'dependencies': [
                '<(DEPTH)/src/llvm/tf/tf.gyp:libtf',
            ],
            'direct_dependent_settings': {
                'libraries': [
                    '<!(llvm-config --libs)',
                    '-ldl',
                    '-lz',
                    '-lpthread',
                    '-lcurses',
                ],
                'ldflags': [
                    '<!(llvm-config --ldflags)',
                ],
                'defines': [
                    'LLVMLOG_LEVEL=<(llvmlog_level)',
                ],
                'cflags_cc': [
                    '<!(llvm-config --cxxflags)',
                ],
                'cflags': [
                    '<!(llvm-config --cflags)',
                ],
            },
            'cflags_cc': [
                '<!(llvm-config --cxxflags)',
            ],
            'cflags': [
                '<!(llvm-config --cflags)',
            ],
        },
    ],
}
