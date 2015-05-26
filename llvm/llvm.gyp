{
    'includes': [
        'llvm.gypi',
    ],
    'targets': [
        {
            'target_name': 'libllvm',
            'type': 'static_library',
            'sources': [ '<@(sources)',],
            'include_dirs': [
                '.',
            ],
            'direct_dependent_settings': {
                'include_dirs': [
                    '.',
                ],
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
            },
        },
    ],
}
