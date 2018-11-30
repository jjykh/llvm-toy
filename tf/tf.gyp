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
            'direct_dependent_settings': {
                'include_dirs': [
                    '.',
                ],
            },
        },
    ],
}
