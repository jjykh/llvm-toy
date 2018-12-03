{
    'includes': [
        '../../build/common.gypi',
    ],
    'targets': [
        {
            'target_name': 'test-liveness',
            'type': 'executable',
            'sources': [
                'test-liveness.cc'
             ],
            'dependencies': [
                '<(DEPTH)/llvm/llvm.gyp:libllvm',
                '<(DEPTH)/tf/tf.gyp:libtf',
            ]
         },
        {
            'target_name': 'test-tf-builder',
            'type': 'executable',
            'sources': [
                'test-tf-builder.cc'
             ],
            'dependencies': [
                '<(DEPTH)/llvm/llvm.gyp:libllvm',
                '<(DEPTH)/tf/tf.gyp:libtf',
            ]
         },
    ],
}
