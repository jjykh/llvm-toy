{
    'includes': [
        'tf.gypi',
    ],
    'targets': [
        {
            'target_name': 'libtf',
            'type': 'static_library',
            'sources': [ '<@(sources)',],
            'include_dirs': [
                '.',
            ],
            'dependencies': [
                '<(DEPTH)/llvm/llvm.gyp:libllvm',
            ],
            'direct_dependent_settings': {
                'include_dirs': [
                    '.',
                ],
            },
        },
    ],
}
