{
    'includes': [
        '../../../../build/common.gypi',
    ],
    'targets': [
        {
            'target_name': 'test-parser',
            'type': 'executable',
            'sources': [
                'test-parser.cc'
             ],
            'dependencies': [
                '<(DEPTH)/src/llvm/tf/tf.gyp:libtf',
            ]
        },
    ],
}

