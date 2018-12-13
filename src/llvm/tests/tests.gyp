{
    'includes': [
        '../../../build/common.gypi',
    ],
    'targets': [
        {
            'target_name': 'test-liveness',
            'type': 'executable',
            'sources': [
                'test-liveness.cc'
             ],
            'dependencies': [
                '<(DEPTH)/src/llvm/llvm.gyp:libllvm',
                '<(DEPTH)/src/llvm/tf/tf.gyp:libtf',
            ]
         },
        {
            'target_name': 'test-tf-builder',
            'type': 'executable',
            'sources': [
                'test-tf-builder.cc'
             ],
            'dependencies': [
                '<(DEPTH)/src/llvm/llvm.gyp:libllvm',
                '<(DEPTH)/src/llvm/tf/tf.gyp:libtf',
            ]
         },
    ],
}
