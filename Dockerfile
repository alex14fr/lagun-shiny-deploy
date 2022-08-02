FROM quay.io/prometheus/busybox
RUN wget https://janona.perso.math.cnrs.fr/shiny-rootfs.tar.xz && mkdir rootfs && tar xf shiny-rootfs.tar.xz -C rootfs && rm shiny-rootfs.tar.xz

FROM scratch 
COPY --from=0 rootfs/* /
EXPOSE 8080 
CMD [ "/entry" ]


