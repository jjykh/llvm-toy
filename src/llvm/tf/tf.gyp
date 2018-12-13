{
    'includes': [
        'tf.gypi',
    ],
    'targets': [
        {
            'target_name': 'libtf',
            'type': 'static_library',
            'sources': [ '<@(sources)',],
        },
    ],
}
