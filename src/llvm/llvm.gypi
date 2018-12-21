{
    'includes': [
        '../../build/common.gypi',
    ],
    'variables': {
        'sources': [
            'initialize-llvm.cc',
            'log.cpp',
            'compiler-state.cc',
            'intrinsic-repository.cc',
            'common-values.cc',
            'output.cc',
            'compile.cc',
            'basic-block.cc',
            'basic-block-manager.cc',
            'liveness-analysis-visitor.cc',
            'llvm-tf-builder.cc',
            'stack-map-info.cc',
        ],
        'llvmlog_level': 0,
    },
}
