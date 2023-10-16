# Using the latest Ubuntu LTS arm64 image
FROM amd64/ubuntu:22.04

# Define bazel version
ARG BAZEL_VERSION=5.3.0

ENV ANDROID_DEV_HOME /android
RUN mkdir -p ${ANDROID_DEV_HOME}

ENV ANDROID_SDK_FILENAME commandlinetools-linux-9645777_latest.zip
ENV ANDROID_SDK_URL https://dl.google.com/android/repository/${ANDROID_SDK_FILENAME}

ENV ANDROID_API_LEVEL 30
# min API level supported by NDK
ENV ANDROID_NDK_API_LEVEL 23
ENV ANDROID_BUILD_TOOLS_VERSION 30.0.3
ENV ANDROID_SDK_HOME ${ANDROID_DEV_HOME}/sdk
RUN mkdir -p ${ANDROID_SDK_HOME}/cmdline-tools
ENV PATH ${PATH}:${ANDROID_SDK_HOME}/cmdline-tools/latest/bin:${ANDROID_SDK_HOME}/platform-tools
ENV ANDROID_NDK_FILENAME android-ndk-r21e-linux-x86_64.zip
ENV ANDROID_NDK_URL https://dl.google.com/android/repository/${ANDROID_NDK_FILENAME}
ENV ANDROID_NDK_HOME ${ANDROID_DEV_HOME}/ndk
ENV TOOLCHAIN=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64

# Configure and build.
ENV PATH ${PATH}:${ANDROID_NDK_HOME}:${TOOLCHAIN}/bin

# Define user id
ARG UID=1000


RUN apt-get update -y

RUN apt-get install -y sudo \
      git \
      g++ \
      gdb \
      clang \
      curl \
      python3-numpy \
      gnupg \
      apt-transport-https \
      openjdk-11-jdk \
      make \
      ninja-build \
      cmake \
      autoconf \
      automake \
      libtool \
      locales-all \
      dos2unix \
      rsync \
      tar \
      unzip \
  && apt-get clean


# Install Android SDK.
RUN cd ${ANDROID_DEV_HOME} && \
    curl --progress-bar -L -O ${ANDROID_SDK_URL} && \
    unzip ${ANDROID_SDK_FILENAME} -d /tmp && \
    mv /tmp/cmdline-tools ${ANDROID_SDK_HOME}/cmdline-tools/latest && \
    rm ${ANDROID_SDK_FILENAME}


# Install Android NDK.
RUN cd ${ANDROID_DEV_HOME} && \
    curl --progress-bar -A "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:90.0) Gecko/20100101 Firefox/90.0" -O ${ANDROID_NDK_URL} && \
    unzip ${ANDROID_NDK_FILENAME} -d ${ANDROID_DEV_HOME} && \
    rm ${ANDROID_NDK_FILENAME} && \
    bash -c "ln -s ${ANDROID_DEV_HOME}/android-ndk-* ${ANDROID_NDK_HOME}"


# Make android ndk executable to all users.
RUN chmod -R go=u ${ANDROID_DEV_HOME}

# add build user to sudoers
RUN echo "builder ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/builder


# Download and install Bazel
RUN curl -fL --progress-bar -O https://github.com/bazelbuild/bazel/releases/download/${BAZEL_VERSION}/bazel-${BAZEL_VERSION}-installer-linux-x86_64.sh && \
chmod +x bazel-${BAZEL_VERSION}-installer-linux-x86_64.sh 

RUN ./bazel-${BAZEL_VERSION}-installer-linux-x86_64.sh && \
rm -f bazel-${BAZEL_VERSION}-installer-linux-x86_64.sh;


# Set the locale
ENV LANG en_US.UTF-8
ENV LANGUAGE en_US:en
ENV LC_ALL en_US.UTF-8


# Create a new user using the provided user id
RUN useradd -m -u ${UID} -s /bin/bash builder

# Switch to the new user
USER builder

CMD ["/bin/bash"]
