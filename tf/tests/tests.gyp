{
    'includes': [
        '../../build/common.gypi',
    ],
    'targets': [
        {
            'target_name': 'test-parser',
            'type': 'executable',
            'sources': [
                'test-parser.cc'
             ],
            'dependencies': [
                '<(DEPTH)/tf/tf.gyp:libtf',
            ]
        },
    ],
}

