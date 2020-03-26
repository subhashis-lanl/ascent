#
# run at root of ascent repo
#
date
# setup dest dir
export ASCENT_VERSION=0.5.2-pre
export DEST_DIR=$WORLDWORK/csc340/software/ascent/${ASCENT_VERSION}/summit/cuda/gnu
# make sure dest dir hierarchy exists
mkdir -p ${DEST_DIR}
# run spack install, this will install ascent@develop
python scripts/uberenv/uberenv.py --spec="%gcc" \
       --install \
       --spack-config-dir="scripts/uberenv/spack_configs/olcf/summit/" \
       --prefix=${DEST_DIR}

# gen env helper script
rm public_env.sh
python scripts/spack_install/gen_public_install_env_script.py ${DEST_DIR} gcc/6.4.0 cuda
chmod a+x public_env.sh
cp public_env.sh $WORLDWORK/csc340/software/ascent/${ASCENT_VERSION}/summit/ascent_summit_setup_env_gcc_cuda.sh
date